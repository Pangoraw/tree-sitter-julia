#include <tree_sitter/parser.h>
#include <wctype.h>
#include <stdio.h>

#define DEBUG(x) printf("SCANNER: %s\n", x);
#define DEBUG_LOOKAHEAD(x) printf("SCANNER: lookahead is: 0x%x %c\n", x, x);

enum TokenType {
  BLOCK_COMMENT,
  IMMEDIATE_PAREN,

  STRING_DELIM,
  TRIPLE_STRING_DELIM,

  STRING_CONTENT,
  STRING_CONTENT_NO_INTERPOLATION,
  TRIPLE_STRING_CONTENT,
  TRIPLE_STRING_CONTENT_NO_INTERPOLATION,
};

static void debug_valid_symbol(const bool *valid_symbols) {
  if (valid_symbols[BLOCK_COMMENT]) {
    printf("BLOCK_COMMENT = true\n");
  }
  if (valid_symbols[IMMEDIATE_PAREN]) {
    printf("IMMEDIATE_PAREN = true\n");
  }
  if (valid_symbols[STRING_DELIM]) {
    printf("STRING_DELIM = true\n");
  }
  if (valid_symbols[TRIPLE_STRING_DELIM]) {
    printf("TRIPLE_STRING_DELIM = true\n");
  }
  if (valid_symbols[STRING_CONTENT]) {
    printf("STRING_CONTENT = true\n");
  }
  if (valid_symbols[STRING_CONTENT_NO_INTERPOLATION]) {
    printf("STRING_CONTENT_NO_INTERPOLATION = true\n");
  }
  if (valid_symbols[TRIPLE_STRING_CONTENT]) {
    printf("TRIPLE_STRING_CONTENT = true\n");
  }
  if (valid_symbols[TRIPLE_STRING_CONTENT_NO_INTERPOLATION]) {
    printf("TRIPLE_STRING_CONTENT_NO_INTERPOLATION = true\n");
  }
}

static bool string_delim(TSLexer *lexer, const bool *valid_symbols) {
  // DEBUG("entering string_delim");

  if (lexer->lookahead != '"') {
    return false;
  }

  lexer->advance(lexer, false);
  lexer->mark_end(lexer);

  // Try consuming the last 2
  int quote_count = 1;
  while (quote_count < 3 && lexer->lookahead == '"') {
    lexer->advance(lexer, false);
    quote_count++;
  }

  if (quote_count < 3) {
    if (!valid_symbols[STRING_DELIM]) {
      return false;
    }
    lexer->result_symbol = STRING_DELIM;
    return true;
  }

  if (!valid_symbols[TRIPLE_STRING_DELIM]) return false;

  lexer->mark_end(lexer);
  lexer->result_symbol = TRIPLE_STRING_DELIM;
  return true;
}

static bool string_content(TSLexer *lexer, const bool *valid_symbols) {
  // debug_valid_symbol(valid_symbols);

  bool triple_string = valid_symbols[TRIPLE_STRING_CONTENT] ||
    valid_symbols[TRIPLE_STRING_CONTENT_NO_INTERPOLATION];

  if (lexer->lookahead == '"') {
    if (valid_symbols[STRING_DELIM] || valid_symbols[TRIPLE_STRING_DELIM]) {
        return string_delim(lexer, valid_symbols);
    } else if (!triple_string) {
        return false;
    } else {
        lexer->mark_end(lexer);

        int quote_count = 0;
        while (quote_count < 3 && lexer->lookahead == '"') {
            lexer->advance(lexer, false);
            quote_count++;
        }

        if (quote_count == 3) {
            return false;
        }
    }
  }

  bool interpolate = valid_symbols[STRING_CONTENT] ||
    valid_symbols[TRIPLE_STRING_CONTENT];

  if (interpolate && lexer->lookahead == '$') {
    return false;
  }

  while (true) {
    // DEBUG_LOOKAHEAD(lexer->lookahead);
    switch (lexer->lookahead) {
      case '"':
        if (!triple_string) {
          lexer->result_symbol = interpolate ? STRING_CONTENT : STRING_CONTENT_NO_INTERPOLATION;
          return true;
        }
        lexer->mark_end(lexer);

        int quote_count = 0;
        while (quote_count < 3 && lexer->lookahead == '"') {
          lexer->advance(lexer, false);
          quote_count++;
        }

        if (quote_count == 3) {
          lexer->result_symbol = interpolate ? TRIPLE_STRING_CONTENT : TRIPLE_STRING_CONTENT_NO_INTERPOLATION;
          return true;
        }

        continue;
      case '$':
        if (!interpolate) {
          lexer->advance(lexer, false);
          continue;
        }
        lexer->mark_end(lexer);
        lexer->result_symbol = triple_string ? TRIPLE_STRING_CONTENT : STRING_CONTENT;
        return true;
      case '\\':
        lexer->advance(lexer, false);
        if (lexer->lookahead == '\0') {
            lexer->mark_end(lexer);
            lexer->result_symbol = triple_string ?
                (interpolate ? TRIPLE_STRING_CONTENT : TRIPLE_STRING_CONTENT_NO_INTERPOLATION) :
                (interpolate ? STRING_CONTENT : STRING_CONTENT_NO_INTERPOLATION);
            return true;
        }
        lexer->advance(lexer, false);

        break;
      case '\0':
        lexer->mark_end(lexer);
        lexer->result_symbol = triple_string ?
            (interpolate ? TRIPLE_STRING_CONTENT : TRIPLE_STRING_CONTENT_NO_INTERPOLATION) :
            (interpolate ? STRING_CONTENT : STRING_CONTENT_NO_INTERPOLATION);
        return true;
      default:
        lexer->advance(lexer, false);
    }
  }

  return false;
}

bool tree_sitter_julia_external_scanner_scan(
  void *payload,
  TSLexer *lexer,
  const bool *valid_symbols
) {
  // debug_valid_symbol(valid_symbols);

  if (
    lexer->lookahead == '(' &&
    valid_symbols[IMMEDIATE_PAREN]
  ) {
    lexer->result_symbol = IMMEDIATE_PAREN;
    return true;
  }

  if (valid_symbols[STRING_CONTENT] || valid_symbols[STRING_CONTENT_NO_INTERPOLATION] ||
      valid_symbols[TRIPLE_STRING_CONTENT] || valid_symbols[TRIPLE_STRING_CONTENT_NO_INTERPOLATION]) {
    return string_content(lexer, valid_symbols);
  }

  while (iswspace(lexer->lookahead)) {
    lexer->advance(lexer, true);
  }

  if (lexer->lookahead == '#') {
    lexer->advance(lexer, false);
    if (lexer->lookahead != '=') {
      return false;
    }
    lexer->advance(lexer, false);

    bool after_eq = false;
    unsigned nesting_depth = 1;
    for (;;) {
      switch (lexer->lookahead) {
        case '\0':
          return false;
        case '=':
          lexer->advance(lexer, false);
          after_eq = true;
          break;
        case '#':
          if (after_eq) {
            lexer->advance(lexer, false);
            after_eq = false;
            nesting_depth--;
            if (nesting_depth == 0) {
              lexer->result_symbol = BLOCK_COMMENT;
              return true;
            }
          } else {
            lexer->advance(lexer, false);
            after_eq = false;
            if (lexer->lookahead == '=') {
              nesting_depth++;
              lexer->advance(lexer, false);
            }
          }
          break;
        default:
          lexer->advance(lexer, false);
          after_eq = false;
          break;
      }
    }
  }

  return string_delim(lexer, valid_symbols);
}

void *tree_sitter_julia_external_scanner_create() {
  return NULL;
}

void tree_sitter_julia_external_scanner_destroy(void *payload) {}

unsigned tree_sitter_julia_external_scanner_serialize(
  void *payload,
  char *buffer
) {
  return 0;
}

void tree_sitter_julia_external_scanner_deserialize(
  void *payload,
  const char *buffer,
  unsigned length
) {}
