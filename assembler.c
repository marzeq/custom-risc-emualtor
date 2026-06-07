#define _POSIX_C_SOURCE 202405L

#define USE_STR_VIEW_UTIL
#include "utils.h"

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "isa.h"

static const str_view sv_ip = { 2, "ip" };
static const str_view sv_sp = { 2, "sp" };
static const str_view sv_flags = { 5, "flags" };
static const str_view sv_machine_info = { 12, "machine_info" };
static const str_view sv_ivt = { 3, "ivt" };

static const str_view sv_entry = { 6, ".entry" };
static const str_view sv_byte  = { 5, ".byte"  };
static const str_view sv_quad  = { 5, ".quad"  };
static const str_view sv_ascii = { 6, ".ascii" };
static const str_view sv_zero  = { 5, ".zero"  };

static inline bool sv_empty(str_view sv) {
  return sv.count == 0;
}

typedef struct {
  str_view name;
  str_view scope;
  u64 address;
} label;

typedef struct {
  label* items;
  usz count;
  usz capacity;
} label_list;

typedef struct {
  char* data;
  usz size;
} buffer;

typedef struct {
  str_view current_global;
  label_list labels;
} assembler_context;

void die(const char* message) {
  fprintf(stderr, "Error: %s\n", message);
  exit(1);
}

typedef struct {
  const char* file;
  usz line;
} source_location;

void error_at(source_location loc, const char* format, ...) {
  fprintf(stderr, "%s:%zu: ", loc.file, loc.line);

  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  fputc('\n', stderr);
  exit(1);
}

void dief(const char* format, ...) {
  fprintf(stderr, "error: ");

  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  fputc('\n', stderr);
  exit(1);
}

void* xrealloc(void* pointer, usz count, usz size) {
  if (count != 0 && size > SIZE_MAX / count) {
    die("allocation overflow");
  }
  void* result = realloc(pointer, count * size);
  if (!result) {
    die("out of memory");
  }
  return result;
}

bool is_ident_start(char ch) {
  return isalpha((unsigned char)ch) || ch == '_' || ch == '.';
}

bool is_ident_char(char ch) {
  return isalnum((unsigned char)ch) || ch == '_' || ch == '.';
}

bool str_view_eq_ignore_case(str_view a, str_view b) {
  if (a.count != b.count) {
    return false;
  }

  for (usz i = 0; i < a.count; i++) {
    if (tolower((unsigned char)a.data[i]) != tolower((unsigned char)b.data[i])) {
      return false;
    }
  }

  return true;
}

int parse_line_marker(str_view line, char* filename_buf) {
  if (sv_empty(line) || line.data[0] != '#') {
    return 0;
  }

  str_view_chop_left(&line, 1);
  line = str_view_trim_left(line);

  u64 line_no = 0;
  usz digits = 0;

  while (digits < line.count && isdigit((unsigned char)line.data[digits])) {
    line_no = line_no * 10 + (u64)(line.data[digits] - '0');

    digits++;
  }

  if (digits == 0) {
    return 0;
  }

  str_view_chop_left(&line, digits);
  line = str_view_trim_left(line);

  if (line.count < 2 || line.data[0] != '"') {
    return 0;
  }

  str_view_chop_left(&line, 1);

  usz end = 0;

  while (end < line.count && line.data[end] != '"') {
    end++;
  }

  if (end >= line.count) {
    return 0;
  }

  memcpy(filename_buf, line.data, end);
  filename_buf[end] = '\0';

  return (int)line_no;
}

str_view normalize_line(str_view line) {
  usz comment_pos = line.count;

  for (usz i = 0; i < line.count; i++) {
    if (line.data[i] == '#') {
      comment_pos = i;
      break;
    }

    if (line.data[i] == '/' && i + 1 < line.count && line.data[i + 1] == '/') {
      comment_pos = i;
      break;
    }
  }

  line.count = comment_pos;

  return str_view_trim(line);
}

str_view next_token(str_view* cursor) {
  while (cursor->count && ( isspace((unsigned char)cursor->data[0]) || cursor->data[0] == ',')) {
    str_view_chop_left(cursor, 1);
  }

  if (cursor->count == 0) {
    return (str_view){0};
  }

  if (cursor->data[0] == '"') {
    str_view_chop_left(cursor, 1);

    usz i = 0;

    while (i < cursor->count) {
      if (cursor->data[i] == '"' && (i == 0 || cursor->data[i - 1] != '\\')) {
        str_view result = str_view_from_parts(cursor->data, i);

        str_view_chop_left(cursor, i + 1);

        return result;
      }

      i++;
    }

    return (str_view){0};
  }

  if (cursor->data[0] == '\'') {
    usz i = 1;

    while (i < cursor->count) {
      if (cursor->data[i] == '\'' && cursor->data[i - 1] != '\\') {
        str_view result = str_view_from_parts(cursor->data, i + 1);

        str_view_chop_left(cursor, i + 1);

        return result;
      }

      i++;
    }

    return (str_view){0};
  }

  usz i = 0;

  while (i < cursor->count && !isspace((unsigned char)cursor->data[i]) && cursor->data[i] != ',') {
    i++;
  }

  return str_view_chop_left(cursor, i);
}

usz decode_string_literal(str_view literal, char* output) {
  usz written = 0;

  for (usz i = 0; i < literal.count; i++) {
    char ch = literal.data[i];

    if (ch == '\\') {
      i++;

      if (i >= literal.count) {
        return 0;
      }

      switch (literal.data[i]) {
        case 'n':  ch = '\n'; break;
        case 'r':  ch = '\r'; break;
        case 't':  ch = '\t'; break;
        case '0':  ch = '\0'; break;
        case '\\': ch = '\\'; break;
        case '"':  ch = '"'; break;

        default:
          return 0;
      }
    }

    if (output) {
      output[written] = ch;
    }

    written++;
  }

  return written;
}

usz decoded_string_literal_size(str_view literal) {
  return decode_string_literal(literal, NULL);
}

void label_list_add(
  assembler_context* ctx,
  str_view name,
  str_view scope,
  u64 address,
  source_location loc
) {
  label_list* labels = &ctx->labels;
  for (usz i = 0; i < labels->count; i++) {
    if (str_view_eq(labels->items[i].name, name) && str_view_eq(labels->items[i].scope, scope)) {
      error_at(
        loc,
        "duplicate label '%.*s'",
        (int)name.count,
        name.data
      );
    }
  }

  if (labels->count == labels->capacity) {
    labels->capacity =
      labels->capacity == 0
      ? 16
      : labels->capacity * 2;

    labels->items = xrealloc(labels->items, labels->capacity, sizeof(label));
  }

  labels->items[labels->count++] = (label) {
    .name = name,
    .scope = scope,
    .address = address,
  };
}

bool find_label(
  assembler_context* ctx,
  str_view name,
  u64* address
) {
  for (usz i = 0; i < ctx->labels.count; i++) {
    label* item = &ctx->labels.items[i];

    if (!str_view_eq(item->name, name)) {
      continue;
    }

    if (name.data[0] == '.') {
      if (!str_view_eq(item->scope, ctx->current_global)) {
        continue;
      }
    } else {
      if (!sv_empty(item->scope)) {
        continue;
      }
    }

    *address = item->address;
    return true;
  }

  return false;
}

bool parse_u64(str_view sv, int base, u64* value) {
  if (sv_empty(sv)) {
    return false;
  }

  u64 result = 0;

  for (usz i = 0; i < sv.count; i++) {
    unsigned digit;
    char ch = sv.data[i];

    if (ch >= '0' && ch <= '9') {
      digit = (unsigned)(ch - '0');
    } else if (base == 16 && ch >= 'a' && ch <= 'f') {
      digit = 10u + (unsigned)(ch - 'a');
    } else if (base == 16 && ch >= 'A' && ch <= 'F') {
      digit = 10u + (unsigned)(ch - 'A');
    } else {
      return false;
    }

    if (digit >= (unsigned)base) {
      return false;
    }

    result = result * (u64)base + (u64)digit;
  }

  *value = result;
  return true;
}

bool parse_register(str_view token, u8* value) {
  if (token.count >= 2 && (token.data[0] == 'r' || token.data[0] == 'R') && isdigit((unsigned char)token.data[1])) {
    u64 reg = 0;

    for (usz i = 1; i < token.count; i++) {
      if (!isdigit((unsigned char)token.data[i])) {
        return false;
      }

      reg = reg * 10 + (u64)(token.data[i] - '0');
    }

    if (reg >= GENERAL_REGISTER_COUNT) {
      return false;
    }

    *value = (u8)reg;
    return true;
  }

  if (str_view_eq_ignore_case(token, sv_ip)) {
    *value = (u8)reserved_register_index(REG_SLOT_IP);
    return true;
  }

  if (str_view_eq_ignore_case(token, sv_sp)) {
    *value = (u8)reserved_register_index(REG_SLOT_SP);
    return true;
  }

  if (str_view_eq_ignore_case(token, sv_flags)) {
    *value = (u8)reserved_register_index(REG_SLOT_FLAGS);
    return true;
  }

  if (str_view_eq_ignore_case(token, sv_machine_info)) {
    *value = (u8)reserved_register_index(REG_SLOT_MACHINE_INFO);
    return true;
  }

  if (str_view_eq_ignore_case(token, sv_ivt)) {
    *value = (u8)reserved_register_index(REG_SLOT_IVT);
    return true;
  }

  return false;
}

bool parse_imm_or_label(
  str_view token,
  assembler_context* ctx,
  u64* value
) {
  if (
    token.count >= 3 &&
    token.data[0] == '\'' &&
    token.data[token.count - 1] == '\''
  ) {
    if (token.count == 3) {
      *value = (u64)(unsigned char)token.data[1];
      return true;
    }

    if (token.count == 4 && token.data[1] == '\\') {
      switch (token.data[2]) {
        case 'n': *value = '\n'; return true;
        case 'r': *value = '\r'; return true;
        case 't': *value = '\t'; return true;
        case '0': *value = '\0'; return true;
        case '\\': *value = '\\'; return true;
        case '\'': *value = '\''; return true;
      }

      return false;
    }

    return false;
  }

  u64 parsed;

  if (
    token.count > 2 &&
    token.data[0] == '0' &&
    (token.data[1] == 'x' || token.data[1] == 'X')
  ) {
    if (!parse_u64(str_view_from_parts(token.data + 2, token.count - 2), 16, &parsed)) {
      return false;
    }

    *value = parsed;
    return true;
  }

  if (parse_u64(token, 10, &parsed)) {
    if (parsed > UINT32_MAX) {
      return false;
    }

    *value = parsed;
    return true;
  }

  return find_label(ctx, token, value);
}

bool opcode_from_mnemonic(str_view token, opcode* value) {
  if (str_view_eq_ignore_case(token, str_view_from_cstr("halt")))      { *value = OP_HALT; return true; }

  // data movement

  if (str_view_eq_ignore_case(token, str_view_from_cstr("loadi")))     { *value = OP_LOADI; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("mov")))       { *value = OP_MOV; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("load")))      { *value = OP_LOAD; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("store")))     { *value = OP_STORE; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("lea")))       { *value = OP_LEA; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("loadb")))     { *value = OP_LOADB; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("storeb")))    { *value = OP_STOREB; return true; }

  // arithmetic

  if (str_view_eq_ignore_case(token, str_view_from_cstr("add")))       { *value = OP_ADD; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("sub")))       { *value = OP_SUB; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("mul")))       { *value = OP_MUL; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("div")))       { *value = OP_DIV; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("mod")))       { *value = OP_MOD; return true; }

  if (str_view_eq_ignore_case(token, str_view_from_cstr("addi")))      { *value = OP_ADDI; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("subi")))      { *value = OP_SUBI; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("muli")))      { *value = OP_MULI; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("divi")))      { *value = OP_DIVI; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("modi")))      { *value = OP_MODI; return true; }

  // bitwise

  if (str_view_eq_ignore_case(token, str_view_from_cstr("and")))       { *value = OP_AND; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("or")))        { *value = OP_OR; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("xor")))       { *value = OP_XOR; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("not")))       { *value = OP_NOT; return true; }

  if (str_view_eq_ignore_case(token, str_view_from_cstr("andi")))      { *value = OP_ANDI; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("ori")))       { *value = OP_ORI; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("xori")))      { *value = OP_XORI; return true; }

  if (str_view_eq_ignore_case(token, str_view_from_cstr("shl")))       { *value = OP_SHL; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("shr")))       { *value = OP_SHR; return true; }

  if (str_view_eq_ignore_case(token, str_view_from_cstr("shli")))      { *value = OP_SHLI; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("shri")))      { *value = OP_SHRI; return true; }

  // compare / branch

  if (str_view_eq_ignore_case(token, str_view_from_cstr("cmp")))       { *value = OP_CMP; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("cmpi")))      { *value = OP_CMPI; return true; }

  if (str_view_eq_ignore_case(token, str_view_from_cstr("jmp")))       { *value = OP_JMP; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("jmpr")))      { *value = OP_JMPR; return true; }

  if (str_view_eq_ignore_case(token, str_view_from_cstr("je")))        { *value = OP_JE; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("jne")))       { *value = OP_JNE; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("jl")))        { *value = OP_JL; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("jle")))       { *value = OP_JLE; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("jg")))        { *value = OP_JG; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("jge")))       { *value = OP_JGE; return true; }

  // calls

  if (str_view_eq_ignore_case(token, str_view_from_cstr("call")))      { *value = OP_CALL; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("callr")))     { *value = OP_CALLR; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("ret")))       { *value = OP_RET; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("int")))       { *value = OP_INT; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("iret")))      { *value = OP_IRET; return true; }

  // stack

  if (str_view_eq_ignore_case(token, str_view_from_cstr("push")))      { *value = OP_PUSH; return true; }
  if (str_view_eq_ignore_case(token, str_view_from_cstr("pop")))       { *value = OP_POP; return true; }

  // misc

  if (str_view_eq_ignore_case(token, str_view_from_cstr("nop")))       { *value = OP_NOP; return true; }

  return false;
}

bool opcode_from_line(str_view line, opcode* value) {
  str_view cursor = line;
  str_view mnemonic = next_token(&cursor);

  return mnemonic.count != 0 && opcode_from_mnemonic(mnemonic, value);
}

bool find_label_candidate(str_view text, usz* colon_index) {
  if (sv_empty(text) || !is_ident_start(text.data[0])) {
    return false;
  }

  usz i = 1;

  while (i < text.count && is_ident_char(text.data[i])) {
    i++;
  }

  if (i >= text.count || text.data[i] != ':') {
    return false;
  }

  *colon_index = i;
  return true;
}

str_view consume_labels(
  str_view line,
  assembler_context* ctx,
  u64 address,
  source_location loc
) {
  str_view cursor = str_view_trim(line);

  while (cursor.count) {
    usz colon;

    if (!find_label_candidate(cursor, &colon)) {
      break;
    }

    str_view label_name =
      str_view_from_parts(cursor.data, colon);

    if (label_name.data[0] == '.') {
      if (sv_empty(ctx->current_global)) {
        error_at(loc, "local label without parent scope");
      }

      label_list_add(
        ctx,
        label_name,
        ctx->current_global,
        address,
        loc
      );
    } else {
      ctx->current_global = label_name;

      label_list_add(
        ctx,
        label_name,
        (str_view){0},
        address,
        loc
      );
    }

    str_view_chop_left(&cursor, colon + 1);
    cursor = str_view_trim_left(cursor);
  }

  return cursor;
}

str_view skip_labels(
  str_view line,
  assembler_context* ctx
) {
  str_view cursor = str_view_trim(line);

  while (cursor.count) {
    usz colon;

    if (!find_label_candidate(cursor, &colon)) {
      break;
    }

    str_view label_name =
      str_view_from_parts(cursor.data, colon);

    if (label_name.data[0] != '.') {
      ctx->current_global = label_name;
    } else if (sv_empty(ctx->current_global)) {
      // should never happen if pass 1 succeeded
      break;
    }

    str_view_chop_left(&cursor, colon + 1);
    cursor = str_view_trim_left(cursor);
  }

  return cursor;
}

void expect_no_extra(str_view cursor, source_location loc) {
  cursor = str_view_trim_left(cursor);

  if (cursor.count != 0) {
    error_at(loc, "unexpected trailing tokens");
  }
}

usz assemble_line(
  str_view line,
  assembler_context* ctx,
  u8* output,
  source_location loc
) {
  str_view cursor = normalize_line(line);

  if (sv_empty(cursor)) {
    return 0;
  }

  str_view mnemonic = next_token(&cursor);

  if (sv_empty(mnemonic)) {
    return 0;
  }

  opcode op = 0;

  if (!opcode_from_mnemonic(mnemonic, &op)) {
    error_at(loc, "unknown instruction");
  }

  u8 a = 0;
  u8 b = 0;
  u8 c = 0;
  u64 imm = 0;
  str_view token = {0};

  switch (op) {
    case OP_HALT:
      expect_no_extra(cursor, loc);
      break;

    case OP_LOADI: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &a)) {
        error_at(loc, "expected destination register");
      }
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_imm_or_label(token, ctx, &imm)) {
        error_at(loc, "expected immediate or label");
      }
      expect_no_extra(cursor, loc);
      break;
    }

    case OP_MOV: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &a)) {
        error_at(loc, "expected destination register");
      }
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &b)) {
        error_at(loc, "expected source register");
      }
      expect_no_extra(cursor, loc);
      break;
    }

    case OP_LOAD:
    case OP_STORE:
    case OP_LOADB:
    case OP_STOREB:
    case OP_LEA: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &a)) {
        error_at(loc, "expected register operand");
      }

      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &b)) {
        error_at(loc, "expected base register");
      }

      token = next_token(&cursor);
      if (sv_empty(token) || !parse_imm_or_label(token, ctx, &imm)) {
        error_at(loc, "expected displacement");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_AND:
    case OP_OR:
    case OP_XOR: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &a)) {
        error_at(loc, "expected destination register");
      }

      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &b)) {
        error_at(loc, "expected source register");
      }

      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &c)) {
        error_at(loc, "expected source register");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_ADDI:
    case OP_SUBI:
    case OP_MULI:
    case OP_DIVI:
    case OP_MODI:
    case OP_ANDI:
    case OP_ORI:
    case OP_XORI: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &a)) {
        error_at(loc, "expected destination register");
      }

      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &b)) {
        error_at(loc, "expected source register");
      }

      token = next_token(&cursor);
      if (sv_empty(token) || !parse_imm_or_label(token, ctx, &imm)) {
        error_at(loc, "expected immediate");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_NOT: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &a)) {
        error_at(loc, "expected destination register");
      }

      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &b)) {
        error_at(loc, "expected source register");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_SHL:
    case OP_SHR: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &a)) {
        error_at(loc, "expected destination register");
      }

      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &b)) {
        error_at(loc, "expected source register");
      }

      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &c)) {
        error_at(loc, "expected shift amount register");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_SHLI:
    case OP_SHRI: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &a)) {
        error_at(loc, "expected destination register");
      }

      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &b)) {
        error_at(loc, "expected source register");
      }

      token = next_token(&cursor);
      if (sv_empty(token) || !parse_imm_or_label(token, ctx, &imm)) {
        error_at(loc, "expected shift amount");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_CMP: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &a)) {
        error_at(loc, "expected first compare register");
      }

      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &b)) {
        error_at(loc, "expected second compare register");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_CMPI: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &a)) {
        error_at(loc, "expected compare register");
      }

      token = next_token(&cursor);
      if (sv_empty(token) || !parse_imm_or_label(token, ctx, &imm)) {
        error_at(loc, "expected immediate");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_JMP:
    case OP_JE:
    case OP_JNE:
    case OP_JL:
    case OP_JLE:
    case OP_JG:
    case OP_JGE: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_imm_or_label(token, ctx, &imm)) {
        error_at(loc, "expected jump target");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_JMPR: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &a)) {
        error_at(loc, "expected jump target register");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_CALL: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_imm_or_label(token, ctx, &imm)) {
        error_at(loc, "expected call target");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_CALLR: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &a)) {
        error_at(loc, "expected call target register");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_RET: {
      expect_no_extra(cursor, loc);
      break;
    }

    case OP_INT: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_imm_or_label(token, ctx, &imm)) {
        error_at(loc, "expected interrupt number");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_IRET: {
      expect_no_extra(cursor, loc);
      break;
    }

    case OP_PUSH:
    case OP_POP: {
      token = next_token(&cursor);
      if (sv_empty(token) || !parse_register(token, &a)) {
        error_at(loc, "expected register operand");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_NOP: {
      expect_no_extra(cursor, loc);
      break;
    }
  }

#define output_insn(type, ...) \
  do { \
    instruction_##type insn = { (u8)op, __VA_ARGS__ }; \
    memcpy(output, &insn, sizeof(insn)); \
    return sizeof(insn); \
  } while (0);

  switch (opcode_instruction_type(op)) {
    case INSN_TYPE_0REG:
      output_insn(0reg);
      break;

    case INSN_TYPE_1REG: 
      output_insn(1reg, a);
      break;

    case INSN_TYPE_2REG:
      output_insn(2reg, a, b);
      break;

    case INSN_TYPE_3REG:
      output_insn(3reg, a, b, c);
      break;

    case INSN_TYPE_0REG_IMM:
      output_insn(0reg_imm, imm);
      break;

    case INSN_TYPE_1REG_IMM:
      output_insn(1reg_imm, a, imm);
      break;

    case INSN_TYPE_2REG_IMM:
      output_insn(2reg_imm, a, b, imm);
      break;
  }
}

usz emit_directive(
  str_view line,
  assembler_context* ctx,
  u8* output,
  source_location loc
) {
  str_view cursor = normalize_line(line);

  if (sv_empty(cursor) || cursor.data[0] != '.') {
    error_at(loc, "directive must start with '.'");
  }

  str_view directive = next_token(&cursor);

  if (sv_empty(directive)) {
    error_at(loc, "expected directive name");
  }

  if (str_view_eq_ignore_case(directive, sv_entry)) {
    return 0;
  }

  if (str_view_eq_ignore_case(directive, sv_byte)) {
    str_view token = next_token(&cursor);

    if (sv_empty(token)) {
      error_at(loc, "expected byte value");
    }

    u64 value = 0;

    if (!parse_imm_or_label(token, ctx, &value) || value > 0xFF) {
      error_at(loc, "expected byte value in range 0-255");
    }

    *output = (u8)value;
    return 1;
  }

  if (str_view_eq_ignore_case(directive, sv_quad)) {
    str_view token = next_token(&cursor);

    if (sv_empty(token)) {
      error_at(loc, "expected quad value");
    }

    u64 value = 0;

    if (!parse_imm_or_label(token, ctx, &value)) {
      error_at(loc, "expected quad value");
    }

    memcpy(output, &value, sizeof(value));
    return sizeof(value);
  }

  if (str_view_eq_ignore_case(directive, sv_ascii)) {
    str_view token = next_token(&cursor);

    if (sv_empty(token)) {
      error_at(loc, "expected string literal");
    }

    return decode_string_literal(token, (char*)output);
  }

  if (str_view_eq_ignore_case(directive, sv_zero)) {
    str_view token = next_token(&cursor);

    if (sv_empty(token)) {
      error_at(loc, "expected size value");
    }

    u64 value = 0;

    if (!parse_imm_or_label(token, ctx, &value)) {
      error_at(loc, "expected size value");
    }

    memset(output, 0, (size_t)value);
    return (usz)value;
  }

  error_at(loc, "unknown directive");
  return 0;
}

void print_help(const char* program) {
  printf("Usage: %s source.asm output.bin\n", program);
}

buffer preprocess_file(const char* path, char* cpp_args[], int cpp_argc) {
  char command[4096];

  int written = snprintf(command, sizeof(command), "cpp");

  for (int i = 0; i < cpp_argc; i++) {
    written += snprintf(
      command + written,
      sizeof(command) - (size_t)written,
      " %s",
      cpp_args[i]
    );
  }

  snprintf(
    command + written,
    sizeof(command) - (size_t)written,
    " \"%s\"",
    path
  );

  FILE* pipe = popen(command, "r");
  if (!pipe) {
    dief("could not run preprocessor on '%s'", path);
  }

  buffer result = {0};
  char chunk[4096];

  while (fgets(chunk, sizeof(chunk), pipe)) {
    usz chunk_length = strlen(chunk);

    result.data = xrealloc(
      result.data,
      result.size + chunk_length + 1,
      sizeof(char)
    );

    memcpy(result.data + result.size, chunk, chunk_length);
    result.size += chunk_length;
    result.data[result.size] = '\0';
  }

  if (pclose(pipe) != 0) {
    free(result.data);
    dief("preprocessor failed on '%s'", path);
  }

  for (usz i = 0; i < result.size; i++) {
    if (result.data[i] == ';') {
      result.data[i] = '\n';
    }
  }

  return result;
}

usz assembled_instruction_size(str_view line) {
  opcode op = 0;

  if (!opcode_from_line(line, &op)) {
    return 0;
  }

  return size_for_instruction_type(
    opcode_instruction_type(op)
  );
}

usz directive_size(
  str_view line,
  str_view* entry_point,
  bool* shift_labels,
  source_location loc
) {
  str_view cursor = line;
  str_view directive = next_token(&cursor);

  if (sv_empty(directive)) {
    error_at(loc, "expected directive name");
  }

  if (str_view_eq_ignore_case(directive, sv_entry)) {
    if (entry_point->count != 0) {
      error_at(loc, "multiple entry point directives are not allowed");
    }

    str_view token = next_token(&cursor);

    if (sv_empty(token)) {
      error_at(loc, "expected entry point label");
    }

    if (token.data[0] == '.') {
      error_at(loc, "entry point cannot be a local label");
    }

    *entry_point = token;
    *shift_labels = false;

    return size_for_instruction_type(opcode_instruction_type(OP_JMP));
  }

  if (str_view_eq_ignore_case(directive, sv_byte)) {
    return 1;
  }

  if (str_view_eq_ignore_case(directive, sv_quad)) {
    return 8;
  }

  if (str_view_eq_ignore_case(directive, sv_ascii)) {
    str_view token = next_token(&cursor);

    if (sv_empty(token)) {
      error_at(loc, "expected string literal");
    }

    return decoded_string_literal_size(token);
  }

  if (str_view_eq_ignore_case(directive, sv_zero)) {
    str_view token = next_token(&cursor);

    if (sv_empty(token)) {
      error_at(loc, "expected size value");
    }

    u64 value = 0;

    if (!parse_imm_or_label(token, NULL, &value)) {
      error_at(loc, "expected size value");
    }

    return (usz)value;
  }

  error_at(loc, "unknown directive");
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    print_help(argv[0]);
    return 1;
  }

  if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
    print_help(argv[0]);
    return 0;
  }

  int cpp_argc = 0;
  char* cpp_args[argc];

  for (int i = 1; i < argc - 2; i++) {
    if (strncmp(argv[i], "-D", 2) == 0 ||
        strncmp(argv[i], "-I", 2) == 0 ||
        strncmp(argv[i], "-U", 2) == 0) {
      cpp_args[cpp_argc++] = argv[i];
    } else {
      fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
      return 1;
    }
  }

  const char* input_path = argv[argc - 2];
  const char* output_path = argv[argc - 1];

  buffer source = preprocess_file(input_path, cpp_args, cpp_argc);

  char* first_pass_source = malloc(source.size + 1);
  if (!first_pass_source) {
    free(source.data);
    die("out of memory");
  }
  memcpy(first_pass_source, source.data, source.size + 1);

  char* second_pass_source = malloc(source.size + 1);
  if (!second_pass_source) {
    free(first_pass_source);
    free(source.data);
    die("out of memory");
  }
  memcpy(second_pass_source, source.data, source.size + 1);

  str_view entry_point = {0};
  usz output_size = 0;
  usz label_address_offset = 0;
  assembler_context ctx = {0};

  source_location loc = {
    .file = input_path,
    .line = 1,
  };

  char marker_file[1024];

  str_view first_pass = str_view_from_parts(source.data, source.size);

  while (first_pass.count) {
    str_view line = str_view_chop_by_delim(&first_pass, '\n');

    int marker_line = parse_line_marker(line, marker_file);

    if (marker_line > 0) {
      loc.file = marker_file;
      loc.line = (usz)marker_line;
      continue;
    }

    str_view cursor = consume_labels(line, &ctx, (u64)label_address_offset, loc);

    cursor = normalize_line(cursor);

    if (sv_empty(cursor)) {
      loc.line++;
      continue;
    }

    usz s = 0;
    bool shift_labels = true;

    if (cursor.data[0] == '.') {
      s = directive_size(cursor, &entry_point, &shift_labels, loc);
    } else {
      s = assembled_instruction_size(cursor);
    }

    if (s == 0) {
      error_at(loc, "unknown instruction");
    }

    output_size += s;

    if (shift_labels) {
      label_address_offset += s;
    }

    loc.line++;
  }

  u64 entry_address = 0;

  if (entry_point.count != 0 && !find_label(&ctx, entry_point, &entry_address)) {
    dief("entry point label '%.*s' not found", svpfarg(entry_point));
  }

  if (entry_point.count != 0) {
    usz jmp_size = size_for_instruction_type(opcode_instruction_type(OP_JMP));

    for (usz i = 0; i < ctx.labels.count; i++) {
      ctx.labels.items[i].address += jmp_size;
    }
  }

  loc.file = input_path;
  loc.line = 1;

  u8* output = malloc(output_size);

  if (!output && output_size > 0) {
    die("out of memory");
  }

  str_view second_pass = str_view_from_parts(source.data, source.size);

  usz output_offset = 0;

  if (entry_point.count != 0) {
    output_offset += size_for_instruction_type(opcode_instruction_type(OP_JMP));
  }

  ctx.current_global = (str_view){0};

  while (second_pass.count) {
    str_view line = str_view_chop_by_delim(&second_pass, '\n');

    int marker_line = parse_line_marker(line, marker_file);

    if (marker_line > 0) {
      loc.file = marker_file;
      loc.line = (usz)marker_line;
      continue;
    }

    str_view cursor = skip_labels(line, &ctx);

    cursor = normalize_line(cursor);

    if (sv_empty(cursor)) {
      loc.line++;
      continue;
    }

    usz written;

    if (cursor.data[0] == '.') {
      written = emit_directive(cursor, &ctx, output + output_offset, loc);
    } else {
      written = assemble_line(cursor, &ctx, output + output_offset, loc);
    }

    output_offset += written;
    loc.line++;
  }

  if (entry_point.count != 0) {
    u64 entry_address = 0;

    if (!find_label(&ctx, entry_point, &entry_address)) {
      dief("entry point label '%.*s' not found", svpfarg(entry_point));
    }

    instruction_0reg_imm entry_insn = {
      .opcode = OP_JMP,
      .imm = entry_address,
    };

    memcpy(output, &entry_insn, sizeof(entry_insn));
  }

  FILE* out = stdout;
  if (strcmp(output_path, "-") != 0) {
    out = fopen(output_path, "wb");
    if (!out) {
      dief("could not open output file '%s'", output_path);
    }
  }
  if (output_size > 0) {
    size_t written = fwrite(output, 1, output_size, out);
    if (written != output_size) {
      if (out != stdout) {
        fclose(out);
      }
      dief("could not write output file '%s'", output_path);
    }
  }
  if (out != stdout) {
    fclose(out);
  } else {
    fflush(stdout);
  }

  free(ctx.labels.items);
  free(output);
  free(source.data);

  return 0;
}
