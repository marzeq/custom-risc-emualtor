#define _POSIX_C_SOURCE 202405L

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "isa.h"

typedef struct {
  char* name;
  u32 address;
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

static void die(const char* message) {
  fprintf(stderr, "Error: %s\n", message);
  exit(1);
}

typedef struct {
  const char* file;
  usz line;
} source_location;

static void error_at(source_location loc, const char* format, ...) {
  fprintf(stderr, "%s:%zu: ", loc.file, loc.line);

  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  fputc('\n', stderr);
  exit(1);
}

static void dief(const char* format, const char* detail) {
  fprintf(stderr, "error: ");
  fprintf(stderr, format, detail);
  fputc('\n', stderr);
  exit(1);
}

static void* xrealloc(void* pointer, usz count, usz size) {
  if (count != 0 && size > SIZE_MAX / count) {
    die("allocation overflow");
  }
  void* result = realloc(pointer, count * size);
  if (!result) {
    die("out of memory");
  }
  return result;
}

static char* duplicate_range(const char* start, usz length) {
  char* copy = malloc(length + 1);
  if (!copy) {
    die("out of memory");
  }
  memcpy(copy, start, length);
  copy[length] = '\0';
  return copy;
}

static bool is_ident_start(char ch) {
  return isalpha((unsigned char)ch) || ch == '_' || ch == '.';
}

static bool is_ident_char(char ch) {
  return isalnum((unsigned char)ch) || ch == '_' || ch == '.';
}

static bool equals_ignore_case(const char* left, const char* right) {
  while (*left && *right) {
    if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
      return false;
    }
    left++;
    right++;
  }
  return *left == '\0' && *right == '\0';
}

static char* trim_left(char* text) {
  while (*text && isspace((unsigned char)*text)) {
    text++;
  }
  return text;
}

static void trim_right(char* text) {
  usz length = strlen(text);
  while (length > 0 && isspace((unsigned char)text[length - 1])) {
    text[length - 1] = '\0';
    length--;
  }
}

static int parse_line_marker(const char* line, char* filename_buf) {
  int line_no = 0;

  if (*line != '#') {
    return 0;
  }

  if (sscanf(line, "# %d \"%1023[^\"]\"", &line_no, filename_buf) != 2) {
    return 0;
  }

  return line_no;
}

static char* normalize_line(char* line) {
  char* cpp_comment = strchr(line, '#');
  char* asm_comment = strstr(line, "//");

  char* comment = NULL;

  if (cpp_comment && asm_comment) {
    comment = cpp_comment < asm_comment
      ? cpp_comment
      : asm_comment;
  } else if (cpp_comment) {
    comment = cpp_comment;
  } else {
    comment = asm_comment;
  }

  if (comment) {
    *comment = '\0';
  }

  trim_right(line);
  return trim_left(line);
}

static char* next_token(char** cursor) {
  char* text = *cursor;

  while (*text && (isspace((unsigned char)*text) || *text == ',')) {
    text++;
  }

  if (*text == '\0') {
    *cursor = text;
    return NULL;
  }

  char* start = text;

  if (*text == '\'') {
    text++;

    if (*text == '\\' && text[1] != '\0') {
      text += 2;
    } else if (*text != '\0') {
      text++;
    }

    if (*text == '\'') {
      text++;
    }

    if (*text) {
      *text++ = '\0';
    }

    *cursor = text;
    return start;
  }

  while (*text && !isspace((unsigned char)*text) && *text != ',') {
    text++;
  }

  if (*text) {
    *text++ = '\0';
  }

  *cursor = text;
  return start;
}

static void label_list_add(label_list* labels, const char* name, usz length, u32 address, source_location loc) {
  for (usz i = 0; i < labels->count; i++) {
    if (strcmp(labels->items[i].name, name) == 0) {
      error_at(loc, "duplicate label '%.*s'", (int)length, name);
      exit(1);
    }
  }

  if (labels->count == labels->capacity) {
    labels->capacity = labels->capacity == 0 ? 16 : labels->capacity * 2;
    labels->items = xrealloc(labels->items, labels->capacity, sizeof(label));
  }

  labels->items[labels->count].name = duplicate_range(name, length);
  labels->items[labels->count].address = address;
  labels->count++;
}

static bool find_label(const label_list* labels, const char* name, u64* address) {
  for (usz i = 0; i < labels->count; i++) {
    if (strcmp(labels->items[i].name, name) == 0) {
      *address = labels->items[i].address;
      return true;
    }
  }
  return false;
}

static bool parse_u64_value(const char* token, u64* value) {
  errno = 0;

  char* end = NULL;
  unsigned long long parsed = strtoull(token, &end, 0);

  if (token[0] == '\0' || end == token || *end != '\0') {
    return false;
  }

  if (errno == ERANGE) {
    return false;
  }

  *value = (u64)parsed;
  return true;
}

static bool parse_u64_hex_value(const char* token, u64* value) {
  if (token[0] != '0' || tolower((unsigned char)token[1]) != 'x') {
    return false;
  }

  errno = 0;

  char* end = NULL;
  unsigned long long parsed = strtoull(token + 2, &end, 16);

  if (end == token + 2 || *end != '\0') {
    return false;
  }

  if (errno == ERANGE) {
    return false;
  }

  *value = (u64)parsed;
  return true;
}

static bool parse_register(const char* token, u8* value) {
  if ((token[0] == 'r' || token[0] == 'R') && isdigit((unsigned char)token[1])) {
    char* end = NULL;
    unsigned long parsed = strtoul(token + 1, &end, 10);
    if (end == token + 1 || *end != '\0') {
      return false;
    }
    if (parsed >= GENERAL_REGISTER_COUNT) {
      return false;
    }
    *value = (u8)parsed;
    return true;
  }

  if (equals_ignore_case(token, "pc")) {
    *value = (u8)reserved_register_index(REG_SLOT_PC);
    return true;
  }
  if (equals_ignore_case(token, "sp")) {
    *value = (u8)reserved_register_index(REG_SLOT_SP);
    return true;
  }
  if (equals_ignore_case(token, "flags")) {
    *value = (u8)reserved_register_index(REG_SLOT_FLAGS);
    return true;
  }
  if (equals_ignore_case(token, "machine_info")) {
    *value = (u8)reserved_register_index(REG_SLOT_MACHINE_INFO);
    return true;
  }

  return false;
}

static bool parse_imm_or_label(const char* token, const label_list* labels, u64* value) {
  if (token[0] == '\'') {
    unsigned char ch;
    size_t len = strlen(token);

    if (len == 3 && token[2] == '\'') {
      ch = (unsigned char)token[1];
      *value = (u64)ch;
      return true;
    }

    if (len == 4 && token[1] == '\\' && token[3] == '\'') {
      switch (token[2]) {
        case 'n':  ch = '\n'; break;
        case 'r':  ch = '\r'; break;
        case 't':  ch = '\t'; break;
        case '0':  ch = '\0'; break;
        case '\\': ch = '\\'; break;
        case '\'': ch = '\''; break;
        default: return false;
      }

      *value = (u64)ch;
      return true;
    }

    return false;
  }

  u64 parsed = 0;

  if (token[0] == '0' && tolower((unsigned char)token[1]) == 'x') {
    if (!parse_u64_hex_value(token, &parsed)) {
      return false;
    }
    *value = parsed;
    return true;
  }

  if (parse_u64_value(token, &parsed)) {
    if (parsed > UINT32_MAX) {
      return false;
    }

    *value = parsed;
    return true;
  }

  u64 address = 0;
  if (find_label(labels, token, &address)) {
    *value = address;
    return true;
  }

  return false;
}

static bool opcode_from_mnemonic(const char* token, opcode* value) {
  if (equals_ignore_case(token, "halt"))      { *value = OP_HALT; return true; }

  // data movement

  if (equals_ignore_case(token, "loadi"))     { *value = OP_LOADI; return true; }
  if (equals_ignore_case(token, "mov"))       { *value = OP_MOV; return true; }
  if (equals_ignore_case(token, "load"))      { *value = OP_LOAD; return true; }
  if (equals_ignore_case(token, "store"))     { *value = OP_STORE; return true; }
  if (equals_ignore_case(token, "lea"))       { *value = OP_LEA; return true; }
  if (equals_ignore_case(token, "loadb"))     { *value = OP_LOADB; return true; }
  if (equals_ignore_case(token, "storeb"))    { *value = OP_STOREB; return true; }

  // arithmetic

  if (equals_ignore_case(token, "add"))       { *value = OP_ADD; return true; }
  if (equals_ignore_case(token, "sub"))       { *value = OP_SUB; return true; }
  if (equals_ignore_case(token, "mul"))       { *value = OP_MUL; return true; }
  if (equals_ignore_case(token, "div"))       { *value = OP_DIV; return true; }
  if (equals_ignore_case(token, "mod"))       { *value = OP_MOD; return true; }

  if (equals_ignore_case(token, "addi"))      { *value = OP_ADDI; return true; }
  if (equals_ignore_case(token, "subi"))      { *value = OP_SUBI; return true; }
  if (equals_ignore_case(token, "muli"))      { *value = OP_MULI; return true; }
  if (equals_ignore_case(token, "divi"))      { *value = OP_DIVI; return true; }
  if (equals_ignore_case(token, "modi"))      { *value = OP_MODI; return true; }

  // bitwise

  if (equals_ignore_case(token, "and"))       { *value = OP_AND; return true; }
  if (equals_ignore_case(token, "or"))        { *value = OP_OR; return true; }
  if (equals_ignore_case(token, "xor"))       { *value = OP_XOR; return true; }
  if (equals_ignore_case(token, "not"))       { *value = OP_NOT; return true; }

  if (equals_ignore_case(token, "andi"))      { *value = OP_ANDI; return true; }
  if (equals_ignore_case(token, "ori"))       { *value = OP_ORI; return true; }
  if (equals_ignore_case(token, "xori"))      { *value = OP_XORI; return true; }

  if (equals_ignore_case(token, "shl"))       { *value = OP_SHL; return true; }
  if (equals_ignore_case(token, "shr"))       { *value = OP_SHR; return true; }

  if (equals_ignore_case(token, "shli"))      { *value = OP_SHLI; return true; }
  if (equals_ignore_case(token, "shri"))      { *value = OP_SHRI; return true; }

  // compare / branch

  if (equals_ignore_case(token, "cmp"))       { *value = OP_CMP; return true; }
  if (equals_ignore_case(token, "cmpi"))      { *value = OP_CMPI; return true; }

  if (equals_ignore_case(token, "jmp"))       { *value = OP_JMP; return true; }
  if (equals_ignore_case(token, "jmpr"))      { *value = OP_JMPR; return true; }

  if (equals_ignore_case(token, "je"))        { *value = OP_JE; return true; }
  if (equals_ignore_case(token, "jne"))       { *value = OP_JNE; return true; }
  if (equals_ignore_case(token, "jl"))        { *value = OP_JL; return true; }
  if (equals_ignore_case(token, "jle"))       { *value = OP_JLE; return true; }
  if (equals_ignore_case(token, "jg"))        { *value = OP_JG; return true; }
  if (equals_ignore_case(token, "jge"))       { *value = OP_JGE; return true; }

  // calls

  if (equals_ignore_case(token, "call"))      { *value = OP_CALL; return true; }
  if (equals_ignore_case(token, "callr"))     { *value = OP_CALLR; return true; }
  if (equals_ignore_case(token, "ret"))       { *value = OP_RET; return true; }

  // stack

  if (equals_ignore_case(token, "push"))      { *value = OP_PUSH; return true; }
  if (equals_ignore_case(token, "pop"))       { *value = OP_POP; return true; }

  // misc

  if (equals_ignore_case(token, "nop"))       { *value = OP_NOP; return true; }
  if (equals_ignore_case(token, "dump_reg"))  { *value = OP_DUMP_REG; return true; }
  if (equals_ignore_case(token, "dump_regs")) { *value = OP_DUMP_REGS; return true; }

  return false;
}

static bool opcode_from_line(const char* line, opcode* value) {
  char* copy = strdup(line);
  if (!copy) {
    die("out of memory");
  }
  char* cursor = copy;
  char* mnemonic = next_token(&cursor);
  bool result = mnemonic && opcode_from_mnemonic(mnemonic, value);
  free(copy);
  return result;
}

static char* find_label_candidate(char* text) {
  char* cursor = text;

  if (!is_ident_start(*cursor)) {
    return NULL;
  }

  cursor++;

  while (is_ident_char(*cursor)) {
    cursor++;
  }

  if (*cursor == ':') {
    return cursor;
  }

  return NULL;
}

static char* consume_labels(char* line, label_list* labels, u64 address, source_location loc) {
  char* cursor = normalize_line(line);

  while (*cursor) {
    char* label_end = find_label_candidate(cursor);

    if (!label_end) {
      break;
    }

    char saved = *label_end;
    *label_end = '\0';
    label_list_add(labels, cursor, (usz)(label_end - cursor), address, loc);

    *label_end = saved;

    cursor = trim_left(label_end + 1);
  }

  return cursor;
}

static char* skip_labels(char* line) {
  char* cursor = normalize_line(line);

  while (*cursor) {
    char* label_end = find_label_candidate(cursor);

    if (!label_end) {
      break;
    }

    cursor = trim_left(label_end + 1);
  }

  return cursor;
}

static void expect_no_extra(char* cursor, source_location loc) {
  cursor = trim_left(cursor);

  if (*cursor != '\0') {
    error_at(loc, "unexpected trailing tokens");
  }
}

static usz assemble_line(
  char* line,
  label_list* labels,
  u8* output,
  source_location loc
) {
  char* cursor = normalize_line(line);

  if (*cursor == '\0') {
    return 0;
  }

  char* mnemonic = next_token(&cursor);

  if (!mnemonic) {
    return 0;
  }

  opcode op = 0;

  if (!opcode_from_mnemonic(mnemonic, &op)) {
    error_at(loc, "unknown instruction");
    exit(1);
  }

  u8 a = 0;
  u8 b = 0;
  u8 c = 0;
  u64 imm = 0;
  char* token = NULL;

  switch (op) {
    case OP_HALT: {
      expect_no_extra(cursor, loc);
      break;
    }

    case OP_LOADI: {
      token = next_token(&cursor);
      if (!token || !parse_register(token, &a)) {
        error_at(loc, "expected destination register");
      }
      token = next_token(&cursor);
      if (!token || !parse_imm_or_label(token, labels, &imm)) {
        error_at(loc, "expected immediate or label");
      }
      expect_no_extra(cursor, loc);
      break;
    }

    case OP_MOV: {
      token = next_token(&cursor);
      if (!token || !parse_register(token, &a)) {
        error_at(loc, "expected destination register");
      }
      token = next_token(&cursor);
      if (!token || !parse_register(token, &b)) {
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
      if (!token || !parse_register(token, &a)) {
        error_at(loc, "expected register operand");
      }

      token = next_token(&cursor);
      if (!token || !parse_register(token, &b)) {
        error_at(loc, "expected base register");
      }

      token = next_token(&cursor);
      if (!token || !parse_imm_or_label(token, labels, &imm)) {
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
      if (!token || !parse_register(token, &a)) {
        error_at(loc, "expected destination register");
      }

      token = next_token(&cursor);
      if (!token || !parse_register(token, &b)) {
        error_at(loc, "expected source register");
      }

      token = next_token(&cursor);
      if (!token || !parse_register(token, &c)) {
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
      if (!token || !parse_register(token, &a)) {
        error_at(loc, "expected destination register");
      }

      token = next_token(&cursor);
      if (!token || !parse_register(token, &b)) {
        error_at(loc, "expected source register");
      }

      token = next_token(&cursor);
      if (!token || !parse_imm_or_label(token, labels, &imm)) {
        error_at(loc, "expected immediate");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_NOT: {
      token = next_token(&cursor);
      if (!token || !parse_register(token, &a)) {
        error_at(loc, "expected destination register");
      }

      token = next_token(&cursor);
      if (!token || !parse_register(token, &b)) {
        error_at(loc, "expected source register");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_SHL:
    case OP_SHR: {
      token = next_token(&cursor);
      if (!token || !parse_register(token, &a)) {
        error_at(loc, "expected destination register");
      }

      token = next_token(&cursor);
      if (!token || !parse_register(token, &b)) {
        error_at(loc, "expected source register");
      }

      token = next_token(&cursor);
      if (!token || !parse_register(token, &c)) {
        error_at(loc, "expected shift amount register");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_SHLI:
    case OP_SHRI: {
      token = next_token(&cursor);
      if (!token || !parse_register(token, &a)) {
        error_at(loc, "expected destination register");
      }

      token = next_token(&cursor);
      if (!token || !parse_register(token, &b)) {
        error_at(loc, "expected source register");
      }

      token = next_token(&cursor);
      if (!token || !parse_imm_or_label(token, labels, &imm)) {
        error_at(loc, "expected shift amount");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_CMP: {
      token = next_token(&cursor);
      if (!token || !parse_register(token, &a)) {
        error_at(loc, "expected first compare register");
      }

      token = next_token(&cursor);
      if (!token || !parse_register(token, &b)) {
        error_at(loc, "expected second compare register");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_CMPI: {
      token = next_token(&cursor);
      if (!token || !parse_register(token, &a)) {
        error_at(loc, "expected compare register");
      }

      token = next_token(&cursor);
      if (!token || !parse_imm_or_label(token, labels, &imm)) {
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
      if (!token || !parse_imm_or_label(token, labels, &imm)) {
        error_at(loc, "expected jump target");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_JMPR: {
      token = next_token(&cursor);
      if (!token || !parse_register(token, &a)) {
        error_at(loc, "expected jump target register");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_CALL: {
      token = next_token(&cursor);
      if (!token || !parse_imm_or_label(token, labels, &imm)) {
        error_at(loc, "expected call target");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_CALLR: {
      token = next_token(&cursor);
      if (!token || !parse_register(token, &a)) {
        error_at(loc, "expected call target register");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_RET: {
      expect_no_extra(cursor, loc);
      break;
    }

    case OP_PUSH:
    case OP_POP: {
      token = next_token(&cursor);
      if (!token || !parse_register(token, &a)) {
        error_at(loc, "expected register operand");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_NOP: {
      expect_no_extra(cursor, loc);
      break;
    }

    case OP_DUMP_REG: {
      token = next_token(&cursor);
      if (!token || !parse_register(token, &a)) {
        error_at(loc, "expected register operand");
      }

      expect_no_extra(cursor, loc);
      break;
    }

    case OP_DUMP_REGS: {
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
  char* line,
  u8* output,
  source_location loc
) {
  (void)output;
  char* cursor = normalize_line(line);

  if (*cursor != '.') {
    error_at(loc, "directive must start with '.'");
    exit(1);
  }

  char* directive = next_token(&cursor);

  if (!directive) {
    error_at(loc, "expected directive name");
    exit(1);
  }

  if (equals_ignore_case(directive, ".entry")) {
    return 0;
  }

  error_at(loc, "unknown directive");
  return 0;
}

static void print_help(const char* program) {
  printf("Usage: %s source.asm output.bin\n", program);
}

static buffer preprocess_file(const char* path, char* cpp_args[], int cpp_argc) {
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

static usz assembled_instruction_size(const char* line) {
  opcode op = 0;
  if (!opcode_from_line(line, &op)) {
    return 0;
  }

  return size_for_instruction_type(opcode_instruction_type(op));
}

static usz directive_size(const char* line, char** entry_point, source_location loc) {
  char* copy = strdup(line);
  if (!copy) {
    die("out of memory");
  }
  char* cursor = copy;
  char* directive = next_token(&cursor);

  if (!directive) {
    free(copy);
    error_at(loc, "expected directive name");
    exit(1);
  }

  if (equals_ignore_case(directive, ".entry")) {
    if (*entry_point) {
      free(copy);
      error_at(loc, "multiple entry point directives are not allowed");
      exit(1);
    }

    char* token = next_token(&cursor);
    if (!token) {
      free(copy);
      error_at(loc, "expected entry point label");
      exit(1);
    }

    *entry_point = strdup(token);

    return size_for_instruction_type(opcode_instruction_type(OP_JMP));
  }

  free(copy);
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

  char* entry_point = NULL;
  label_list labels = {0};
  usz output_size = 0;

  source_location loc = {
    .file = input_path,
    .line = 1,
  };

  char marker_file[1024];

  char* first_pass = first_pass_source;
  while (first_pass) {
    char* line = first_pass;
    char* newline = strchr(first_pass, '\n');

    if (newline) {
      *newline = '\0';
      first_pass = newline + 1;
    } else {
      first_pass = NULL;
    }

    int marker_line = parse_line_marker(line, marker_file);

    if (marker_line > 0) {
      loc.file = marker_file;
      loc.line = (usz)marker_line;
      continue;
    }

    char* cursor = consume_labels(line, &labels, (u64)output_size, loc);

    cursor = normalize_line(cursor);

    if (*cursor == '\0') {
      loc.line++;
      continue;
    }

    usz s = 0;
    if (*cursor == '.') {
      s = directive_size(cursor, &entry_point, loc);
    } else {
      s = assembled_instruction_size(cursor);
    }

    if (s == 0) {
      error_at(loc, "unknown instruction");
    }
    output_size += s;

    loc.line++;
  }

  // check for entry point existence before second pass to avoid doing unnecessary work if entry point is missing
  u64 entry_address = 0;
  if (entry_point && !find_label(&labels, entry_point, &entry_address)) {
    dief("entry point label '%s' not found", entry_point);
  }

  loc.file = input_path;
  loc.line = 1;

  u8* output = malloc(output_size);
  if (!output && output_size > 0) {
    die("out of memory");
  }

  loc.file = input_path;
  loc.line = 1;

  char* second_pass = second_pass_source;
  usz output_offset = 0;

  if (entry_point) {
    output_offset += size_for_instruction_type(opcode_instruction_type(OP_JMP));
  }

  while (second_pass) {
    char* line = second_pass;
    char* newline = strchr(second_pass, '\n');

    if (newline) {
      *newline = '\0';
      second_pass = newline + 1;
    } else {
      second_pass = NULL;
    }

    int marker_line = parse_line_marker(line, marker_file);
    if (marker_line > 0) {
      loc.file = strdup(marker_file);
      loc.line = (usz)marker_line;
      continue;
    }

    char* cursor = skip_labels(line);

    if (*cursor == '\0') {
      loc.line++;
      continue;
    }

    usz written = 0;
    if (*cursor == '.') {
      written = emit_directive(cursor, output + output_offset, loc);
    } else {
      written = assemble_line(cursor, &labels, output + output_offset, loc);
    }

    output_offset += written;

    loc.line++;
  }

  if (entry_point) {
    u64 entry_address = 0;
    if (!find_label(&labels, entry_point, &entry_address)) {
      dief("entry point label '%s' not found", entry_point);
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

  for (usz i = 0; i < labels.count; i++) {
    free(labels.items[i].name);
  }
  free(labels.items);
  free(output);
  free(first_pass_source);
  free(second_pass_source);
  free(source.data);
  return 0;
}
