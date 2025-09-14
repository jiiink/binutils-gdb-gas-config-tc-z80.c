/* tc-z80.c -- Assemble code for the Zilog Z80, Z180, EZ80 and ASCII R800
   Copyright (C) 2005-2025 Free Software Foundation, Inc.
   Contributed by Arnold Metselaar <arnold_m@operamail.com>

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "as.h"
#include "safe-ctype.h"
#include "subsegs.h"
#include "elf/z80.h"
#include "dwarf2dbg.h"
#include "dw2gencfi.h"

/* Exported constants.  */
const char comment_chars[] = ";\0";
const char line_comment_chars[] = "#;\0";
const char line_separator_chars[] = "\0";
const char EXP_CHARS[] = "eE\0";
const char FLT_CHARS[] = "RrDdFfSsHh\0";

/* For machine specific options.  */
const char md_shortopts[] = ""; /* None yet.  */

enum options
{
  OPTION_MARCH = OPTION_MD_BASE,
  OPTION_MACH_Z80,
  OPTION_MACH_R800,
  OPTION_MACH_Z180,
  OPTION_MACH_EZ80_Z80,
  OPTION_MACH_EZ80_ADL,
  OPTION_MACH_INST,
  OPTION_MACH_NO_INST,
  OPTION_MACH_IUD,
  OPTION_MACH_WUD,
  OPTION_MACH_FUD,
  OPTION_MACH_IUP,
  OPTION_MACH_WUP,
  OPTION_MACH_FUP,
  OPTION_FP_SINGLE_FORMAT,
  OPTION_FP_DOUBLE_FORMAT,
  OPTION_COMPAT_LL_PREFIX,
  OPTION_COMPAT_COLONLESS,
  OPTION_COMPAT_SDCC
};

#define INS_Z80      (1 << 0)
#define INS_R800     (1 << 1)
#define INS_GBZ80    (1 << 2)
#define INS_Z180     (1 << 3)
#define INS_EZ80     (1 << 4)
#define INS_Z80N     (1 << 5)
#define INS_MARCH_MASK 0xffff

#define INS_IDX_HALF (1 << 16)
#define INS_IN_F_C   (1 << 17)
#define INS_OUT_C_0  (1 << 18)
#define INS_SLI      (1 << 19)
#define INS_ROT_II_LD (1 << 20)  /* instructions like SLA (ii+d),r; which is: LD r,(ii+d); SLA r; LD (ii+d),r */
#define INS_TUNE_MASK 0xffff0000

#define INS_NOT_GBZ80 (INS_Z80 | INS_Z180 | INS_R800 | INS_EZ80 | INS_Z80N)

#define INS_ALL 0
#define INS_UNDOC (INS_IDX_HALF | INS_IN_F_C)
#define INS_UNPORT (INS_OUT_C_0 | INS_SLI | INS_ROT_II_LD)

const struct option md_longopts[] =
{
  { "march",     required_argument, NULL, OPTION_MARCH},
  { "z80",       no_argument, NULL, OPTION_MACH_Z80},
  { "r800",      no_argument, NULL, OPTION_MACH_R800},
  { "z180",      no_argument, NULL, OPTION_MACH_Z180},
  { "ez80",      no_argument, NULL, OPTION_MACH_EZ80_Z80},
  { "ez80-adl",  no_argument, NULL, OPTION_MACH_EZ80_ADL},
  { "fp-s",      required_argument, NULL, OPTION_FP_SINGLE_FORMAT},
  { "fp-d",      required_argument, NULL, OPTION_FP_DOUBLE_FORMAT},
  { "strict",    no_argument, NULL, OPTION_MACH_FUD},
  { "full",      no_argument, NULL, OPTION_MACH_IUP},
  { "with-inst", required_argument, NULL, OPTION_MACH_INST},
  { "Wnins",     required_argument, NULL, OPTION_MACH_INST},
  { "without-inst", required_argument, NULL, OPTION_MACH_NO_INST},
  { "local-prefix", required_argument, NULL, OPTION_COMPAT_LL_PREFIX},
  { "colonless", no_argument, NULL, OPTION_COMPAT_COLONLESS},
  { "sdcc",      no_argument, NULL, OPTION_COMPAT_SDCC},
  { "Fins",      required_argument, NULL, OPTION_MACH_NO_INST},
  { "ignore-undocumented-instructions", no_argument, NULL, OPTION_MACH_IUD },
  { "Wnud",  no_argument, NULL, OPTION_MACH_IUD },
  { "warn-undocumented-instructions",  no_argument, NULL, OPTION_MACH_WUD },
  { "Wud",  no_argument, NULL, OPTION_MACH_WUD },
  { "forbid-undocumented-instructions", no_argument, NULL, OPTION_MACH_FUD },
  { "Fud",  no_argument, NULL, OPTION_MACH_FUD },
  { "ignore-unportable-instructions", no_argument, NULL, OPTION_MACH_IUP },
  { "Wnup",  no_argument, NULL, OPTION_MACH_IUP },
  { "warn-unportable-instructions",  no_argument, NULL, OPTION_MACH_WUP },
  { "Wup",  no_argument, NULL, OPTION_MACH_WUP },
  { "forbid-unportable-instructions", no_argument, NULL, OPTION_MACH_FUP },
  { "Fup",  no_argument, NULL, OPTION_MACH_FUP },

  { NULL, no_argument, NULL, 0 }
} ;

const size_t md_longopts_size = sizeof (md_longopts);

extern int coff_flags;
/* Instruction classes that silently assembled.  */
static int ins_ok = INS_Z80 | INS_UNDOC;
/* Instruction classes that generate errors.  */
static int ins_err = ~(INS_Z80 | INS_UNDOC);
/* eZ80 CPU mode (ADL or Z80) */
static int cpu_mode = 0; /* 0 - Z80, 1 - ADL */
/* accept SDCC specific instruction encoding */
static int sdcc_compat = 0;
/* accept colonless labels */
static int colonless_labels = 0;
/* local label prefix (NULL - default) */
static const char *local_label_prefix = NULL;
/* floating point support */
typedef const char *(*str_to_float_t)(char *litP, int *sizeP);
static str_to_float_t str_to_float;
static str_to_float_t str_to_double;

/* mode of current instruction */
#define INST_MODE_S 0      /* short data mode */
#define INST_MODE_IS 0     /* short instruction mode */
#define INST_MODE_L 2      /* long data mode */
#define INST_MODE_IL 1     /* long instruction mode */
#define INST_MODE_FORCED 4 /* CPU mode changed by instruction suffix*/
static char inst_mode;

struct match_info
{
  const char *name;
  int ins_ok;
  int ins_err;
  int cpu_mode;
  const char *comment;
};

static const struct match_info
match_cpu_table [] =
{
  {"z80",     INS_Z80, 0, 0, "Zilog Z80" },
  {"ez80",    INS_EZ80, 0, 0, "Zilog eZ80" },
  {"gbz80",   INS_GBZ80, INS_UNDOC|INS_UNPORT, 0, "GameBoy Z80" },
  {"r800",    INS_R800, INS_UNPORT, 0, "Ascii R800" },
  {"z180",    INS_Z180, INS_UNDOC|INS_UNPORT, 0, "Zilog Z180" },
  {"z80n",    INS_Z80N, 0, 0, "Z80 Next" }
};

static const struct match_info
match_ext_table [] =
{
  {"full",    INS_UNDOC|INS_UNPORT, 0, 0, "assemble all known instructions" },
  {"adl",     0, 0, 1, "eZ80 ADL mode by default" },
  {"xyhl",    INS_IDX_HALF, 0, 0, "instructions with halves of index registers" },
  {"infc",    INS_IN_F_C, 0, 0, "instruction IN F,(C)" },
  {"outc0",   INS_OUT_C_0, 0, 0, "instruction OUT (C),0" },
  {"sli",     INS_SLI, 0, 0, "instruction known as SLI, SLL, or SL1" },
  {"xdcb",    INS_ROT_II_LD, 0, 0, "instructions like RL (IX+d),R (DD/FD CB dd oo)" }
};


static int signed_overflow (signed long value, unsigned bitsize);
static int unsigned_overflow (unsigned long value, unsigned bitsize);
static int is_overflow (long value, unsigned bitsize);

static void
setup_march (const char *name, int *ok, int *err, int *mode)
{
  const char *segment_start = name;
  size_t segment_len;
  unsigned int i;

  segment_len = strcspn (segment_start, "+-");

  for (i = 0; i < ARRAY_SIZE (match_cpu_table); ++i)
    {
      if (!strncasecmp (segment_start, match_cpu_table[i].name, segment_len) &&
          strlen (match_cpu_table[i].name) == segment_len)
        {
          *ok = match_cpu_table[i].ins_ok;
          *err = match_cpu_table[i].ins_err;
          *mode = match_cpu_table[i].cpu_mode;
          break;
        }
    }

  if (i >= ARRAY_SIZE (match_cpu_table))
    {
      as_fatal (_("Invalid CPU is specified: %s"), name);
    }

  const char *current_pos = segment_start + segment_len;
  while (*current_pos != '\0')
    {
      char operator_char = *current_pos;
      current_pos++;

      segment_start = current_pos;
      segment_len = strcspn (segment_start, "+-");

      for (i = 0; i < ARRAY_SIZE (match_ext_table); ++i)
        {
          if (!strncasecmp (segment_start, match_ext_table[i].name, segment_len) &&
              strlen (match_ext_table[i].name) == segment_len)
            {
              if (operator_char == '+')
                {
                  *ok |= match_ext_table[i].ins_ok;
                  *err &= ~match_ext_table[i].ins_ok;
                  *mode |= match_ext_table[i].cpu_mode;
                }
              else /* operator_char == '-' */
                {
                  *ok &= ~match_ext_table[i].ins_ok;
                  *err |= match_ext_table[i].ins_ok;
                  *mode &= ~match_ext_table[i].cpu_mode;
                }
              break;
            }
        }

      if (i >= ARRAY_SIZE (match_ext_table))
        {
          as_fatal (_("Invalid EXTENSION is specified: %s"), segment_start);
        }

      current_pos = segment_start + segment_len;
    }
}

typedef struct {
  const char *name;
  int value;
} InstructionMapEntry;

static const InstructionMapEntry instruction_map[] = {
    {"idx-reg-halves", INS_IDX_HALF},
    {"sli", INS_SLI},
    {"op-ii-ld", INS_ROT_II_LD},
    {"in-f-c", INS_IN_F_C},
    {"out-c-0", INS_OUT_C_0},
    {NULL, 0}
};

static int
setup_instruction (const char *inst, int *add, int *sub)
{
  int n;
  for (size_t i = 0; instruction_map[i].name != NULL; ++i)
    {
      if (strcmp (inst, instruction_map[i].name) == 0)
        {
          n = instruction_map[i].value;
          *add |= n;
          *sub &= ~n;
          return 1;
        }
    }
  return 0;
}

static const char *
str_to_zeda32 (char *litP, int *sizeP);
static const char *
str_to_float48 (char *litP, int *sizeP);
static const char *
str_to_ieee754_h (char *litP, int *sizeP);
static const char *
str_to_ieee754_s (char *litP, int *sizeP);
static const char *
str_to_ieee754_d (char *litP, int *sizeP);

static str_to_float_t
get_str_to_float (const char *arg)
{
  if (arg == NULL)
    return NULL;

  if (strcasecmp (arg, "zeda32") == 0)
    return str_to_zeda32;
  else if (strcasecmp (arg, "math48") == 0)
    return str_to_float48;
  else if (strcasecmp (arg, "half") == 0)
    return str_to_ieee754_h;
  else if (strcasecmp (arg, "single") == 0)
    return str_to_ieee754_s;
  else if (strcasecmp (arg, "double") == 0)
    return str_to_ieee754_d;
  else if (strcasecmp (arg, "ieee754") == 0)
    as_fatal (_("invalid floating point numbers type `%s'"), arg);

  return NULL;
}

static int
setup_instruction_list (const char *list_str, int *add, int *sub)
{
#define INST_BUF_SIZE 16
  char instruction_buf[INST_BUF_SIZE];
  const char *current_pos = list_str;
  const char *separator_pos;
  size_t instruction_len;
  int successful_count = 0;

  for (current_pos = list_str; *current_pos != '\0';)
    {
      separator_pos = strchr (current_pos, ',');

      if (separator_pos == NULL)
        {
          instruction_len = strlen (current_pos);
        }
      else
        {
          instruction_len = separator_pos - current_pos;
        }

      if (instruction_len == 0 || instruction_len >= INST_BUF_SIZE)
        {
          as_bad (_("invalid INST in command line: %s"), current_pos);
          return 0;
        }

      memcpy (instruction_buf, current_pos, instruction_len);
      instruction_buf[instruction_len] = '\0';

      if (setup_instruction (instruction_buf, add, sub))
        {
          successful_count++;
        }
      else
        {
          as_bad (_("invalid INST in command line: %s"), instruction_buf);
          return 0;
        }

      current_pos += instruction_len;
      if (*current_pos == ',')
        {
          current_pos++;
        }
    }

  return successful_count;
#undef INST_BUF_SIZE
}

static void
md_set_cpu_march (const char* march_name)
{
  setup_march (march_name, &ins_ok, &ins_err, &cpu_mode);
}

int
md_parse_option (int c, const char* arg)
{
  switch (c)
    {
    default:
      return 0;
    case OPTION_MARCH:
      md_set_cpu_march (arg);
      break;
    case OPTION_MACH_Z80:
      md_set_cpu_march ("z80");
      break;
    case OPTION_MACH_R800:
      md_set_cpu_march ("r800");
      break;
    case OPTION_MACH_Z180:
      md_set_cpu_march ("z180");
      break;
    case OPTION_MACH_EZ80_Z80:
      md_set_cpu_march ("ez80");
      break;
    case OPTION_MACH_EZ80_ADL:
      md_set_cpu_march ("ez80+adl");
      break;
    case OPTION_FP_SINGLE_FORMAT:
      str_to_float = get_str_to_float (arg);
      break;
    case OPTION_FP_DOUBLE_FORMAT:
      str_to_double = get_str_to_float (arg);
      break;
    case OPTION_MACH_INST:
      if (! (ins_ok & INS_GBZ80))
        {
          return setup_instruction_list (arg, &ins_ok, &ins_err);
        }
      break;
    case OPTION_MACH_NO_INST:
      if (! (ins_ok & INS_GBZ80))
        {
          return setup_instruction_list (arg, &ins_err, &ins_ok);
        }
      break;
    case OPTION_MACH_WUD:
    case OPTION_MACH_IUD:
      if (! (ins_ok & INS_GBZ80))
        {
          ins_ok |= INS_UNDOC;
          ins_err &= ~INS_UNDOC;
        }
      break;
    case OPTION_MACH_WUP:
    case OPTION_MACH_IUP:
      if (! (ins_ok & INS_GBZ80))
        {
          ins_ok |= (INS_UNDOC | INS_UNPORT);
          ins_err &= ~(INS_UNDOC | INS_UNPORT);
        }
      break;
    case OPTION_MACH_FUD:
      if (! (ins_ok & (INS_R800 | INS_GBZ80)))
        {
	      ins_ok &= (INS_UNDOC | INS_UNPORT);
	      ins_err |= (INS_UNDOC | INS_UNPORT);
        }
      break;
    case OPTION_MACH_FUP:
      ins_ok &= ~INS_UNPORT;
      ins_err |= INS_UNPORT;
      break;
    case OPTION_COMPAT_LL_PREFIX:
      local_label_prefix = (arg && *arg) ? arg : NULL;
      break;
    case OPTION_COMPAT_SDCC:
      sdcc_compat = 1;
      break;
    case OPTION_COMPAT_COLONLESS:
      colonless_labels = 1;
      break;
    }

  return 1;
}

static void
print_table_entries(FILE *f, const struct { const char *name; const char *comment; } *table, unsigned int size)
{
  unsigned int i;
  for (i = 0; i < size; ++i) {
    fprintf(f, "  %-8s\t\t  %s\n", table[i].name, table[i].comment);
  }
}

void
md_show_usage (FILE * f)
{
  fprintf (f, _("\nCPU model options:\n"));
  fprintf (f, _("  -march=CPU[+EXT...][-EXT...]\n"));
  fprintf (f, _("\t\t\t  generate code for CPU, where CPU is one of:\n"));
  
  print_table_entries(f, match_cpu_table, ARRAY_SIZE(match_cpu_table));
  
  fprintf (f, _("And EXT is combination (+EXT - add, -EXT - remove) of:\n"));
  
  print_table_entries(f, match_ext_table, ARRAY_SIZE(match_ext_table));
  
  fprintf (f, _("\nCompatibility options:\n"));
  fprintf (f, _("  -local-prefix=TEXT\t  treat labels prefixed by TEXT as local\n"));
  fprintf (f, _("  -colonless\t\t  permit colonless labels\n"));
  fprintf (f, _("  -sdcc\t\t\t  accept SDCC specific instruction syntax\n"));
  fprintf (f, _("  -fp-s=FORMAT\t\t  set single precision FP numbers format\n"));
  fprintf (f, _("  -fp-d=FORMAT\t\t  set double precision FP numbers format\n"));
  fprintf (f, _("Where FORMAT one of:\n"));
  fprintf (f, _("  ieee754\t\t  IEEE754 compatible (depends on directive)\n"));
  fprintf (f, _("  half\t\t\t  IEEE754 half precision (16 bit)\n"));
  fprintf (f, _("  single\t\t  IEEE754 single precision (32 bit)\n"));
  fprintf (f, _("  double\t\t  IEEE754 double precision (64 bit)\n"));
  fprintf (f, _("  zeda32\t\t  Zeda z80float library 32 bit format\n"));
  fprintf (f, _("  math48\t\t  48 bit format from Math48 library\n"));
  fprintf (f, _("\nDefault: -march=z80+xyhl+infc\n"));
}

static symbolS * zero;

struct reg_entry
{
  const char* name;
  int number;
  int isa;
};
#define R_STACKABLE (0x80)
#define R_ARITH     (0x40)
#define R_IX        (0x20)
#define R_IY        (0x10)
#define R_INDEX     (R_IX | R_IY)

#define REG_A (7)
#define REG_B (0)
#define REG_C (1)
#define REG_D (2)
#define REG_E (3)
#define REG_H (4)
#define REG_L (5)
#define REG_F (6 | 8)
#define REG_I (9)
#define REG_R (10)
#define REG_MB (11)

#define REG_AF (3 | R_STACKABLE)
#define REG_BC (0 | R_STACKABLE | R_ARITH)
#define REG_DE (1 | R_STACKABLE | R_ARITH)
#define REG_HL (2 | R_STACKABLE | R_ARITH)
#define REG_IX (REG_HL | R_IX)
#define REG_IY (REG_HL | R_IY)
#define REG_SP (3 | R_ARITH)

static const struct reg_entry regtable[] =
{
  {"a",   REG_A,        INS_ALL },
  {"af",  REG_AF,       INS_ALL },
  {"b",   REG_B,        INS_ALL },
  {"bc",  REG_BC,       INS_ALL },
  {"c",   REG_C,        INS_ALL },
  {"d",   REG_D,        INS_ALL },
  {"de",  REG_DE,       INS_ALL },
  {"e",   REG_E,        INS_ALL },
  {"f",   REG_F,        INS_IN_F_C | INS_Z80N | INS_R800 },
  {"h",   REG_H,        INS_ALL },
  {"hl",  REG_HL,       INS_ALL },
  {"i",   REG_I,        INS_NOT_GBZ80 },
  {"ix",  REG_IX,       INS_NOT_GBZ80 },
  {"ixh", REG_H | R_IX, INS_IDX_HALF | INS_EZ80 | INS_R800 | INS_Z80N },
  {"ixl", REG_L | R_IX, INS_IDX_HALF | INS_EZ80 | INS_R800 | INS_Z80N },
  {"iy",  REG_IY,       INS_NOT_GBZ80 },
  {"iyh", REG_H | R_IY, INS_IDX_HALF | INS_EZ80 | INS_R800 | INS_Z80N },
  {"iyl", REG_L | R_IY, INS_IDX_HALF | INS_EZ80 | INS_R800 | INS_Z80N },
  {"l",   REG_L,        INS_ALL },
  {"mb",  REG_MB,       INS_EZ80 },
  {"r",   REG_R,        INS_NOT_GBZ80 },
  {"sp",  REG_SP,       INS_ALL },
} ;

#define BUFLEN 8 /* Large enough for any keyword.  */

void
md_begin (void)
{
  expressionS nul_expr, reg_expr;
  char buffer[BUFLEN];
  char *original_input_line_pointer;

  memset(&reg_expr, 0, sizeof(reg_expr));
  memset(&nul_expr, 0, sizeof(nul_expr));

  if (ins_ok & INS_EZ80) {
    listing_lhs_width = 6;
  }

  reg_expr.X_op = O_register;
  reg_expr.X_md = 0;
  reg_expr.X_add_symbol = NULL;
  reg_expr.X_op_symbol = NULL;

  for (unsigned int reg_idx = 0; reg_idx < ARRAY_SIZE(regtable); ++reg_idx) {
    if (regtable[reg_idx].isa && !(regtable[reg_idx].isa & ins_ok)) {
      continue;
    }

    reg_expr.X_add_number = regtable[reg_idx].number;
    const char *reg_name = regtable[reg_idx].name;
    size_t name_len = strlen(reg_name);

    if (name_len >= BUFLEN - 1) {
        continue;
    }

    for (unsigned int case_mask = 0; case_mask < (1U << name_len); ++case_mask) {
      for (unsigned int char_idx = 0; char_idx < name_len; ++char_idx) {
        buffer[char_idx] = (case_mask & (1U << char_idx))
                             ? TOUPPER((unsigned char)reg_name[char_idx])
                             : reg_name[char_idx];
      }
      buffer[name_len] = '\0';

      symbolS *psym = symbol_find_or_make(buffer);
      S_SET_SEGMENT(psym, reg_section);
      symbol_set_value_expression(psym, &reg_expr);
    }
  }

  original_input_line_pointer = input_line_pointer;
  input_line_pointer = (char *) "0";
  nul_expr.X_md = 0;
  expression(&nul_expr);
  input_line_pointer = original_input_line_pointer;
  zero = make_expr_symbol(&nul_expr);

  linkrelax = 0;
}

void
z80_md_finish (void)
{
  int mach_type = 0; /* Initialize mach_type to 0. This value will be used if
                      * none of the switch cases match, mirroring the original
                      * default behavior and making it explicit.
                      */

  switch (ins_ok & INS_MARCH_MASK)
    {
    case INS_Z80:
      mach_type = bfd_mach_z80;
      break;
    case INS_R800:
      mach_type = bfd_mach_r800;
      break;
    case INS_Z180:
      mach_type = bfd_mach_z180;
      break;
    case INS_GBZ80:
      mach_type = bfd_mach_gbz80;
      break;
    case INS_EZ80:
      mach_type = cpu_mode ? bfd_mach_ez80_adl : bfd_mach_ez80_z80;
      break;
    case INS_Z80N:
      mach_type = bfd_mach_z80n;
      break;
    /* No default case is needed here because mach_type is initialized
     * to 0, which correctly handles the scenario where no specific
     * machine type matches (as per the original code's default behavior).
     */
    }
  bfd_set_arch_mach (stdoutput, TARGET_ARCH, mach_type);
}

#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
void
z80_elf_final_processing (void)
{/* nothing to do, all is done by BFD itself */
/*
  unsigned elf_flags;
  elf_elfheader (stdoutput)->e_flags = elf_flags;
*/
}
#endif

#include <ctype.h> // Required for isspace

static const char *
skip_space(const char *s)
{
  // Validate input pointer to prevent dereferencing a NULL pointer,
  // which would lead to undefined behavior and potential crashes.
  if (s == NULL)
  {
    return NULL;
  }

  // Iterate through the string while the current character is a whitespace character.
  // We explicitly check for the null terminator (`*s != '\0'`) to prevent reading
  // past the end of the string.
  // `isspace` expects an `int` argument, whose value must be representable as an
  // `unsigned char` or equal to `EOF`. Casting `*s` to `unsigned char` ensures
  // correctness for systems where `char` is signed.
  while (*s != '\0' && isspace((unsigned char)*s))
  {
    ++s;
  }

  // Return the pointer to the first non-whitespace character, or the null terminator
  // if the string contains only whitespace or is empty.
  return s;
}

/* A non-zero return-value causes a continue in the
   function read_a_source_file () in ../read.c.  */
#include <stdio.h>
#include <string.h>
#include <ctype.h>

extern char *input_line_pointer;
extern int sdcc_compat;

extern void as_bad(const char *);
extern const char *_ (const char *);
extern void ignore_rest_of_line(void);
extern int is_whitespace(char);
extern char *skip_space(char *);
extern int is_name_beginner(char);
extern int ignore_input(void);
extern char get_symbol_name(char **);
extern char restore_line_pointer(char);
extern void bump_line_counters(void);
extern void LISTING_NEWLINE(void);
extern void equals(char *, int);

static int
process_inline_character_modifications (char *line_content_start, int sdcc_compat_flag)
{
  char *p = line_content_start;

  while (*p && *p != '\n')
    {
      switch (*p)
	{
	case '\'':
	  if (p[1] != 0 && p[1] != '\'' && p[2] == '\'')
	    {
	      char buf[4];
	      snprintf (buf, sizeof (buf), "%3d", (unsigned char) p[1]);
	      p[0] = buf[0];
	      p[1] = buf[1];
	      p[2] = buf[2];
	      p += 2;
	      break;
	    }
	case '"':
	  {
	    char quote_char = *p;
	    p++;
	    while (*p != '\n' && *p != quote_char)
	      {
		p++;
	      }
	    if (*p != quote_char)
	      {
		as_bad (_("-- unterminated string"));
		ignore_rest_of_line ();
		return 1;
	      }
	  }
	  break;
	case '#':
	  if (sdcc_compat_flag)
	    {
	      char *next_char_after_hash = p + 1;
	      if (is_whitespace (*next_char_after_hash) && *skip_space (next_char_after_hash) == '(')
		{
		  *p = '0';
		  p++;
		  *p = '+';
		}
	      else
		{
		  *p = (*next_char_after_hash == '(') ? '+' : ' ';
		}
	    }
	  break;
	}
      p++;
    }
  return 0;
}

static void
apply_sdcc_label_transforms (char *line_content_start, int sdcc_compat_flag)
{
  if (!sdcc_compat_flag || *line_content_start != '0')
    {
      return;
    }

  char *p_iter = line_content_start;

  while (*p_iter >= '0' && *p_iter <= '9')
    {
      p_iter++;
    }

  if (p_iter[0] == '$' && p_iter[1] == ':')
    {
      char *dollar_pos = p_iter;
      for (p_iter = line_content_start; *p_iter == '0' && p_iter < dollar_pos - 1; ++p_iter)
	{
	  *p_iter = ' ';
	}
    }
}

static int
handle_label_assignment_statement (int sdcc_compat_flag)
{
  if (!is_name_beginner (*input_line_pointer))
    {
      return 0;
    }

  char *line_start_at_hook = input_line_pointer;

  if (ignore_input ())
    return 0;

  char *symbol_name_start;
  char char_at_new_input_line_pointer;

  char_at_new_input_line_pointer = get_symbol_name (&symbol_name_start);

  char *current_parse_pos = input_line_pointer + 1;

  if (char_at_new_input_line_pointer == ':' && *current_parse_pos == ':')
    {
      if (sdcc_compat_flag)
	{
	  *current_parse_pos = ' ';
	}
      current_parse_pos++;
    }

  current_parse_pos = skip_space (current_parse_pos);

  int assignment_len_chars = 0;
  enum { ASSIGN_NONE, ASSIGN_EQUALS, ASSIGN_DOUBLE_EQUALS, ASSIGN_DOT_EQU, ASSIGN_DOT_DEFL } assignment_type = ASSIGN_NONE;

  if (*current_parse_pos == '=')
    {
      if (current_parse_pos[1] == '=')
	{
	  assignment_len_chars = 2;
	  assignment_type = ASSIGN_DOUBLE_EQUALS;
	}
      else
	{
	  assignment_len_chars = 1;
	  assignment_type = ASSIGN_EQUALS;
	}
    }
  else if (*current_parse_pos == '.')
    {
      char *keyword_start = current_parse_pos + 1;
      if (strncasecmp (keyword_start, "EQU", 3) == 0 && !isalpha (keyword_start[3]))
	{
	  assignment_len_chars = 1 + 3;
	  assignment_type = ASSIGN_DOT_EQU;
	}
      else if (strncasecmp (keyword_start, "DEFL", 4) == 0 && !isalpha (keyword_start[4]))
	{
	  assignment_len_chars = 1 + 4;
	  assignment_type = ASSIGN_DOT_DEFL;
	}
    }

  if (assignment_type != ASSIGN_NONE)
    {
      if (line_start_at_hook[-1] == '\n')
	{
	  bump_line_counters ();
	  LISTING_NEWLINE ();
	}

      input_line_pointer = current_parse_pos + assignment_len_chars;

      switch (assignment_type)
	{
	case ASSIGN_EQUALS:
	case ASSIGN_DOT_DEFL:
	  equals (symbol_name_start, 1);
	  break;
	case ASSIGN_DOUBLE_EQUALS:
	case ASSIGN_DOT_EQU:
	  equals (symbol_name_start, 0);
	  break;
	case ASSIGN_NONE:
	  break;
	}
      return 1;
    }
  else
    {
      (void) restore_line_pointer (char_at_new_input_line_pointer);
      input_line_pointer = line_start_at_hook;
      return 0;
    }
}

int
z80_start_line_hook (void)
{
  if (process_inline_character_modifications (input_line_pointer, sdcc_compat) != 0)
    {
      return 1;
    }

  apply_sdcc_label_transforms (input_line_pointer, sdcc_compat);

  if (handle_label_assignment_statement (sdcc_compat))
    {
      return 1;
    }

  return 0;
}

symbolS *
md_undefined_symbol (char *name ATTRIBUTE_UNUSED)
{
  return NULL;
}

const char *
md_atof (int type, char *litP, int *sizeP)
{
  const char *(*handler_func) (char *, int *) = NULL;

  switch (type)
    {
    case 'f':
    case 'F':
    case 's':
    case 'S':
      handler_func = str_to_float;
      break;
    case 'd':
    case 'D':
    case 'r':
    case 'R':
      handler_func = str_to_double;
      break;
    default:
      // No specific handler for this type, handler_func remains NULL
      break;
    }

  if (handler_func)
    {
      return handler_func (litP, sizeP);
    }

  // If no specific handler was matched for the given type,
  // or if the matched handler function pointer was NULL,
  // fall back to the generic ieee_md_atof implementation.
  return ieee_md_atof (type, litP, sizeP, false);
}

valueT
md_section_align (const segT seg ATTRIBUTE_UNUSED, const valueT size)
{
  return size;
}

long
md_pcrel_from (fixS * fixp)
{
  if (fixp == NULL || fixp->fx_frag == NULL) {
    return 0;
  }
  return fixp->fx_where + fixp->fx_frag->fr_address;
}

typedef const char * (asfunc)(char, char, const char*);

typedef struct _table_t
{
  const char* name;
  unsigned char prefix;
  unsigned char opcode;
  asfunc * fp;
  unsigned inss; /*0 - all CPU types or list of supported INS_* */
} table_t;

/* Compares the key for structs that start with a char * to the key.  */
static int
key_cmp (const void * a, const void * b)
{
  const char *str_a;
  const char *str_b;

  str_a = *((const char**)a);
  str_b = *((const char**)b);

  if (str_a == NULL) {
    if (str_b == NULL) {
      return 0; /* Both are NULL, consider them equal */
    }
    return -1; /* str_a is NULL, str_b is not, str_a comes first */
  }
  if (str_b == NULL) {
    return 1; /* str_a is not NULL, str_b is NULL, str_a comes after */
  }

  /* Both are non-NULL, proceed with standard string comparison */
  return strcmp (str_a, str_b);
}

char buf[BUFLEN];
const char *key = buf;

/* Prevent an error on a line from also generating
   a "junk at end of line" error message.  */
static char err_flag;

static void
error (const char * message)
{
  static int error_already_reported = 0;

  if (error_already_reported)
    return;

  as_bad ("%s", message);
  error_already_reported = 1;
}

#include <libintl.h>
#include <error.h>

static void
ill_op (void)
{
  error(1, 0, _("illegal operand"));
}

static void
wrong_mach (const int ins_type)
{
  if ((ins_type & ins_err) != 0)
    ill_op ();
  else
    as_warn (_("undocumented instruction"));
}

static void
check_mach (int ins_type)
{
  if (!(ins_type & ins_ok))
    wrong_mach (ins_type);
}

/* Check whether an expression is indirect.  */
static const char* skip_quoted_string(const char* p_start) {
    char quote = *p_start;
    const char* p = p_start + 1;

    while (*p != '\0' && *p != '\n' && *p != quote) {
        if (*p == '\\' && p[1] != '\0') {
            p++;
        }
        p++;
    }

    if (*p == quote) {
        p++;
    }
    return p;
}

static int
is_indir (const char *s)
{
  int indir = (*s == '(');
  const char *p = s;
  int depth = 0;

  while (*p != '\0' && *p != ',')
    {
      switch (*p)
	{
	case '"':
	case '\'':
	  p = skip_quoted_string(p);
	  break;
	case '(':
	  ++depth;
	  p++;
	  break;
	case ')':
	  --depth;
	  if (depth == 0)
	    {
	      const char *next_char_after_paren = skip_space (p + 1);
	      if (*next_char_after_paren != '\0' && *next_char_after_paren != ',')
		{
		  indir = 0;
		}
	    }
	  if (depth < 0)
	    {
	      error (_("mismatched parentheses"));
	      return 0;
	    }
	  p++;
	  break;
	default:
	  p++;
	  break;
	}
    }

  if (depth != 0)
    {
      error (_("mismatched parentheses"));
      return 0;
    }

  return indir;
}

/* Check whether a symbol involves a register.  */
static bool
contains_register (symbolS *sym)
{
  if (sym == NULL)
    {
      return false;
    }

  expressionS *ex = symbol_get_value_expression (sym);

  if (ex == NULL)
    {
      return false;
    }

  switch (ex->X_op)
    {
    case O_register:
      return true;

    case O_add:
    case O_subtract:
      if (ex->X_op_symbol != NULL && contains_register (ex->X_op_symbol))
        {
          return true;
        }
      // Fall through logic from original code made explicit here.
      if (ex->X_add_symbol != NULL && contains_register (ex->X_add_symbol))
        {
          return true;
        }
      break;

    case O_uminus:
    case O_symbol:
      if (ex->X_add_symbol != NULL && contains_register (ex->X_add_symbol))
        {
          return true;
        }
      break;

    default:
      break;
    }

  return false;
}

/* Parse general expression, not looking for indexed addressing.  */
static const char *
parse_gbz80_hl_indirect(const char *p_start_paren, expressionS *op)
{
  const char *p_after_paren = skip_space(p_start_paren + 1);

  if (!strncasecmp(p_after_paren, "hl", 2))
    {
      const char *p_after_hl = skip_space(p_after_paren + 2);
      if ((*p_after_hl == '+' || *p_after_hl == '-') && *skip_space(p_after_hl + 1) == ')')
	{
	  op->X_op = O_md1;
	  op->X_add_symbol = NULL;
	  op->X_add_number = (*p_after_hl == '+') ? REG_HL : -REG_HL;
	  return skip_space(p_after_hl + 1) + 1;
	}
    }
  return NULL;
}

static const char *
parse_exp_not_indexed (const char *s, expressionS *op)
{
  const char *current_input_ptr = s;
  int is_indirect = 0;
  int shift_amount = -1;

  memset (op, 0, sizeof (*op));

  current_input_ptr = skip_space (current_input_ptr);

  if (sdcc_compat)
    {
      if (*current_input_ptr == '<')
	{
	  shift_amount = 0;
	  current_input_ptr++;
	}
      else if (*current_input_ptr == '>')
	{
	  shift_amount = cpu_mode ? 16 : 8;
	  current_input_ptr++;
	}
      current_input_ptr = skip_space (current_input_ptr);
    }

  if (shift_amount == -1)
    {
      is_indirect = is_indir (current_input_ptr);
    }
  else
    {
      is_indirect = 0;
    }
  op->X_md = is_indirect;

  if (is_indirect && (ins_ok & INS_GBZ80))
    {
      const char *gbz80_end_ptr = parse_gbz80_hl_indirect(current_input_ptr, op);
      if (gbz80_end_ptr != NULL)
	{
	  input_line_pointer = (char*) gbz80_end_ptr;
	  return input_line_pointer;
	}
    }

  input_line_pointer = (char*) current_input_ptr;
  expression (op);
  resolve_register (op);

  switch (op->X_op)
    {
    case O_absent:
      error (_("missing operand"));
      break;
    case O_illegal:
      error (_("bad expression syntax"));
      break;
    default:
      break;
    }

  if (shift_amount >= 0)
    {
      expressionS shift_value_expr;

      op->X_add_symbol = make_expr_symbol (op);
      op->X_add_number = 0;
      op->X_op = O_right_shift;

      memset (&shift_value_expr, 0, sizeof (shift_value_expr));
      shift_value_expr.X_op = O_constant;
      shift_value_expr.X_add_number = shift_amount;
      
      op->X_op_symbol = make_expr_symbol (&shift_value_expr);
    }
    
  return input_line_pointer;
}

static int
unify_indexed (expressionS *op)
{
  expressionS *val_expr = symbol_get_value_expression (op->X_add_symbol);

  if (!val_expr || O_register != val_expr->X_op)
    {
      return 0;
    }

  int rnum = val_expr->X_add_number;

  if (!(rnum == REG_IX || rnum == REG_IY) || contains_register (op->X_op_symbol))
    {
      ill_op ();
      return 0;
    }

  if (O_subtract == op->X_op)
    {
      expressionS minus_expr_data = {0};
      minus_expr_data.X_op = O_uminus;
      minus_expr_data.X_add_symbol = op->X_op_symbol;

      expressionS *new_op_symbol = make_expr_symbol (&minus_expr_data);
      if (!new_op_symbol)
        {
          return 0;
        }
      op->X_op_symbol = new_op_symbol;
      op->X_op = O_add;
    }

  if (op->X_add_number != 0)
    {
      expressionS add_expr_data = {0};
      add_expr_data.X_op = O_symbol;
      add_expr_data.X_add_number = op->X_add_number;
      add_expr_data.X_add_symbol = op->X_op_symbol;

      expressionS *new_add_symbol = make_expr_symbol (&add_expr_data);
      if (!new_add_symbol)
        {
          return 0;
        }
      op->X_add_symbol = new_add_symbol;
    }
  else
    {
      op->X_add_symbol = op->X_op_symbol;
    }

  op->X_add_number = rnum;
  op->X_op_symbol = NULL;
  return 1;
}

/* Parse expression, change operator to O_md1 for indexed addressing.  */
static const char *
parse_exp_handle_o_constant(const char *s_input, expressionS *op)
{
  if (sdcc_compat && is_indir(s_input))
    {
      expressionS saved_offset_expression = *op;
      const char *new_s = parse_exp(s_input, op);

      if (op->X_op != O_md1 || op->X_add_symbol != zero)
        {
          ill_op();
        }
      else
        {
          op->X_add_symbol = make_expr_symbol(&saved_offset_expression);
        }
      return new_s;
    }
  
  return s_input;
}

static const char *
parse_exp (const char *s, expressionS *op)
{
  const char* res = parse_exp_not_indexed (s, op);
  switch (op->X_op)
    {
    case O_add:
    case O_subtract:
      if (unify_indexed (op) && op->X_md)
        {
          op->X_op = O_md1;
        }
      break;
    case O_register:
      if (op->X_md && (op->X_add_number == REG_IX || op->X_add_number == REG_IY))
        {
          op->X_add_symbol = zero;
          op->X_op = O_md1;
        }
      break;
    case O_constant:
      res = parse_exp_handle_o_constant(res, op);
      break;
    default:
      break;
    }
  return res;
}

/* Condition codes, including some synonyms provided by HiTech zas.  */
static const struct reg_entry cc_tab[] =
{
  { "age", 6 << 3, INS_ALL },
  { "alt", 7 << 3, INS_ALL },
  { "c",   3 << 3, INS_ALL },
  { "di",  4 << 3, INS_ALL },
  { "ei",  5 << 3, INS_ALL },
  { "lge", 2 << 3, INS_ALL },
  { "llt", 3 << 3, INS_ALL },
  { "m",   7 << 3, INS_ALL },
  { "nc",  2 << 3, INS_ALL },
  { "nz",  0 << 3, INS_ALL },
  { "p",   6 << 3, INS_ALL },
  { "pe",  5 << 3, INS_ALL },
  { "po",  4 << 3, INS_ALL },
  { "z",   1 << 3, INS_ALL },
} ;

/* Parse condition code.  */
static const char *
parse_cc (const char *s, char * op)
{
  char local_buf[BUFLEN + 1];
  int i;
  struct reg_entry * cc_found_entry;

  for (i = 0; i < BUFLEN; ++i)
    {
      if (!ISALPHA((unsigned char)s[i]))
	{
	  break;
	}
      local_buf[i] = (char)TOLOWER((unsigned char)s[i]);
    }

  if ((i < BUFLEN) && (s[i] == '\0' || s[i] == ','))
    {
      local_buf[i] = '\0';
      cc_found_entry = bsearch (local_buf, cc_tab, ARRAY_SIZE (cc_tab),
                                sizeof (cc_tab[0]), key_cmp);
    }
  else
    {
      cc_found_entry = NULL;
    }

  if (cc_found_entry)
    {
      *op = cc_found_entry->number;
      return s + i;
    }
  else
    {
      return NULL;
    }
}

static const char *
emit_insn (char prefix, char opcode, const char * args)
{
  char *p;
  size_t size;

  size = prefix ? 2 : 1;

  p = frag_more (size);

  if (p == NULL)
    {
      /*
       * Memory allocation failed.
       * As per external functionality constraints, we must return 'args'.
       * This implies the instruction was not emitted.
       * A more robust design would signal this failure (e.g., via return value
       * or an error indicator), but that would alter external functionality.
       */
      return args;
    }

  if (prefix)
    {
      *p++ = prefix;
    }

  *p = opcode;

  return args;
}

void z80_cons_fix_new (fragS *frag_p, int offset, int nbytes, expressionS *exp)
{
  bfd_reloc_code_real_type reloc_type;

  switch (nbytes)
    {
    case 1:
      reloc_type = BFD_RELOC_8;
      break;
    case 2:
      reloc_type = BFD_RELOC_16;
      break;
    case 3:
      reloc_type = BFD_RELOC_24;
      break;
    case 4:
      reloc_type = BFD_RELOC_32;
      break;
    default:
      as_bad (_("unsupported BFD relocation size %u"), nbytes);
      return;
    }

  fix_new_exp (frag_p, offset, nbytes, exp, 0, reloc_type);
}

static bfd_reloc_code_real_type
get_reloc_type_for_size(int size)
{
    switch (size)
    {
    case 1: return BFD_RELOC_8;
    case 2: return BFD_RELOC_16;
    case 3: return BFD_RELOC_24;
    case 4: return BFD_RELOC_32;
    case 8: return BFD_RELOC_64;
    default:
      as_fatal(_("invalid data size %d"), size);
      return (bfd_reloc_code_real_type)0;
    }
}

static void
handle_constant_expression(char *p, expressionS *val, int size)
{
    if (!val->X_extrabit && is_overflow(val->X_add_number, size * 8))
    {
        as_warn(_("%d-bit overflow (%+" PRId64 ")"), size * 8, (int64_t)val->X_add_number);
    }

    for (int i = 0; i < size; ++i)
    {
        p[i] = (char)((val->X_add_number >> (i * 8)) & 0xff);
    }
}

static bool
is_expression_register_dependent(expressionS *val)
{
    return (val->X_op == O_register)
           || (val->X_op == O_md1)
           || contains_register(val->X_add_symbol)
           || contains_register(val->X_op_symbol);
}

static void
handle_z80_relocation_logic(char **p_ptr, expressionS *val, int *size_ptr, bfd_reloc_code_real_type *r_type_ptr)
{
    char *p = *p_ptr;
    int current_size = *size_ptr;
    bfd_reloc_code_real_type current_r_type = *r_type_ptr;
    bool simplify = true;

    expressionS *op_val_expr = symbol_get_value_expression(val->X_op_symbol);
    if (op_val_expr == NULL) {
        as_fatal(_("invalid operation symbol expression"));
    }

    int shift = op_val_expr->X_add_number;

    if (val->X_op == O_bit_and && shift == ((1 << (current_size * 8)) - 1))
    {
        shift = 0;
    }
    else if (val->X_op != O_right_shift)
    {
        shift = -1;
    }

    if (current_size == 1)
    {
        switch (shift)
        {
        case 0:  current_r_type = BFD_RELOC_Z80_BYTE0; break;
        case 8:  current_r_type = BFD_RELOC_Z80_BYTE1; break;
        case 16: current_r_type = BFD_RELOC_Z80_BYTE2; break;
        case 24: current_r_type = BFD_RELOC_Z80_BYTE3; break;
        default: simplify = false; break;
        }
    }
    else if (current_size == 2)
    {
        switch (shift)
        {
        case 0:  current_r_type = BFD_RELOC_Z80_WORD0; break;
        case 16: current_r_type = BFD_RELOC_Z80_WORD1; break;
        case 8:
        case 24:
        {
            val->X_op = O_symbol;
            val->X_op_symbol = NULL;
            val->X_add_number = 0;

            fix_new_exp(frag_now, (int)(p - frag_now->fr_literal), 1, val, false,
                        (shift == 8) ? BFD_RELOC_Z80_BYTE1 : BFD_RELOC_Z80_BYTE3);

            p++;
            *p_ptr = p;

            current_r_type = (shift == 8) ? BFD_RELOC_Z80_BYTE2 : BFD_RELOC_Z80_BYTE3;
            *size_ptr = 1;
            simplify = false;
            break;
        }
        default: simplify = false; break;
        }
    }
    else {
        simplify = false;
    }

    if (simplify)
    {
        val->X_op = O_symbol;
        val->X_op_symbol = NULL;
        val->X_add_number = 0;
    }

    *r_type_ptr = current_r_type;
}

static void
emit_data_val (expressionS * val, int size)
{
  char *p;
  bfd_reloc_code_real_type r_type;

  p = frag_more (size);

  if (val->X_op == O_constant)
    {
      handle_constant_expression(p, val, size);
      return;
    }

  r_type = get_reloc_type_for_size(size);

  if (is_expression_register_dependent(val))
    {
      ill_op();
    }

  if (size <= 2 && val->X_op_symbol)
    {
      handle_z80_relocation_logic(&p, val, &size, &r_type);
    }

  fix_new_exp(frag_now, (int)(p - frag_now->fr_literal), size, val, false, r_type);
}

static void
emit_byte (expressionS * val, bfd_reloc_code_real_type r_type)
{
  if (r_type == BFD_RELOC_8)
    {
      emit_data_val (val, 1);
      return;
    }

  char *p = frag_more (1);
  *p = (char) val->X_add_number;

  if (contains_register (val->X_add_symbol) || contains_register (val->X_op_symbol))
    {
      ill_op ();
      return;
    }

  if (val->X_op == O_constant)
    {
      if (r_type == BFD_RELOC_8_PCREL)
        {
          as_bad (_("cannot make a relative jump to an absolute location"));
          return;
        }

      if ((val->X_add_number < -128) || (val->X_add_number >= 128))
        {
          if (r_type == BFD_RELOC_Z80_DISP8)
            as_bad (_("index overflow (%+" PRId64 ")"), (int64_t) val->X_add_number);
          else
            as_bad (_("offset overflow (%+" PRId64 ")"), (int64_t) val->X_add_number);
          return;
        }
    }
  else
    {
      fix_new_exp (frag_now, p - (char *)frag_now->fr_literal, 1, val,
                   r_type == BFD_RELOC_8_PCREL, r_type);
    }
}

static const int WORD_SIZE_DEFAULT = 2;
static const int WORD_SIZE_INST_MODE_IL = 3;

static void
emit_word (expressionS * val)
{
  if (val == NULL)
    {
      return;
    }

  const int size_in_bytes = (inst_mode & INST_MODE_IL) ? WORD_SIZE_INST_MODE_IL : WORD_SIZE_DEFAULT;

  emit_data_val (val, size_in_bytes);
}

static void
emit_mx (char primary_prefix_byte, char opcode, int shift, expressionS * arg)
{
  char *p_buffer;
  int register_num = arg->X_add_number;
  char instruction_prefix_idx_byte = 0; /* For 0xDD/0xFD prefix */

  /* Define constants locally if not globally available, for clarity. */
  /* #define REG_M           6   // Represents (HL) or (IX/IY+d) operand type */
  /* #define Z80_PREFIX_IX   0xDD */
  /* #define Z80_PREFIX_IY   0xFD */

  switch (arg->X_op)
    {
    case O_register:
      if (arg->X_md) /* Operand is (HL) */
        {
          if (register_num != REG_HL)
            {
              ill_op ();
              return; /* Prevent further execution on error */
            }
          register_num = REG_M; /* (HL) uses register code 6 for opcode encoding */
        }
      else /* Operand is a register (B, C, D, E, H, L, A, IXH, IXL etc.) */
        {
          /* Handle IXH/IXL/IYH/IYL registers (index register half-bytes). */
          /* These are only allowed if there's no primary_prefix_byte (e.g., CB). */
          if ((primary_prefix_byte == 0) && (register_num & R_INDEX))
            {
              instruction_prefix_idx_byte = (register_num & R_IX) ? Z80_PREFIX_IX : Z80_PREFIX_IY;
              if (!(ins_ok & (INS_EZ80|INS_R800|INS_Z80N)))
                check_mach (INS_IDX_HALF);
              register_num &= ~R_INDEX; /* Clear index bits, leaving H/L register value */
            }

          if (register_num > 7) /* Register number out of standard 0-7 range */
            {
              ill_op ();
              return; /* Prevent further execution on error */
            }
        }

      /* Determine how many bytes to reserve upfront for prefixes and the opcode. */
      int bytes_to_reserve = 1; /* For the final opcode byte itself */
      if (primary_prefix_byte) /* If there's a primary prefix (e.g., CB) */
        bytes_to_reserve++;
      if (instruction_prefix_idx_byte) /* If there's an index prefix (0xDD/0xFD) */
        bytes_to_reserve++;

      p_buffer = frag_more (bytes_to_reserve);

      /* Emit the primary prefix byte if present (e.g., CB) */
      if (primary_prefix_byte)
        *p_buffer++ = primary_prefix_byte;

      /* Emit the index prefix byte if present (e.g., 0xDD/0xFD for IXH/IXL/IYH/IYL) */
      if (instruction_prefix_idx_byte)
        *p_buffer++ = instruction_prefix_idx_byte;
      
      /* Emit the final instruction byte */
      *p_buffer = opcode + (register_num << shift);
      break;

    case O_md1: /* Operand is (IX+d) or (IY+d) */
      if (ins_ok & INS_GBZ80)
        {
          ill_op ();
          return; /* Prevent further execution on error */
        }

      /* Determine instruction index prefix (0xDD or 0xFD) */
      instruction_prefix_idx_byte = (register_num & R_IX) ? Z80_PREFIX_IX : Z80_PREFIX_IY;
      
      /* Reserve 2 bytes initially for the index prefix (DD/FD) and the subsequent byte. */
      /* This subsequent byte can be either the primary_prefix_byte (e.g., CB) or the instruction byte. */
      p_buffer = frag_more(2);
      *p_buffer++ = instruction_prefix_idx_byte; /* Emit 0xDD or 0xFD */

      if (primary_prefix_byte) /* If this is a 0xDD/0xFD CB instruction */
        *p_buffer = primary_prefix_byte; /* Emit CB prefix */
      else
        *p_buffer = opcode + (REG_M << shift); /* Emit the instruction byte for (HL)-equivalent */

      /* Emit the displacement byte. */
      /* Create a temporary expressionS to represent the displacement. */
      expressionS offset_expr = *arg;
      offset_expr.X_op = O_symbol;
      offset_expr.X_add_number = 0; /* Displacement value itself is parsed by emit_byte */
      emit_byte (&offset_expr, BFD_RELOC_Z80_DISP8);

      /* If a primary_prefix_byte was present, the final instruction byte for it */
      /* needs to be emitted after the displacement. This forms the 'DD CB disp byte' sequence. */
      if (primary_prefix_byte)
        {
          p_buffer = frag_more (1); /* Reserve one more byte for the actual opcode */
          *p_buffer = opcode+(REG_M<<shift); /* This is the final instruction byte */
        }
      break;

    default:
      /* If an unexpected operand type is encountered, report an error. */
      /* Replacing abort() with ill_op() assumes ill_op() is the standard */
      /* error reporting mechanism for assembler errors. */
      ill_op();
      return; /* Prevent further execution on error */
    }
}

/* The operand m may be r, (hl), (ix+d), (iy+d),
   if 0 = prefix m may also be ixl, ixh, iyl, iyh.  */
static const char *
emit_m (char prefix, char opcode, const char *args)
{
  expressionS arg_m;
  const char *p;

  p = parse_exp (args, &arg_m);
  switch (arg_m.X_op)
    {
    case O_md1:
    case O_register:
      emit_mx (prefix, opcode, 0, &arg_m);
      break;
    default:
      /*
       * The 'ill_op()' function is assumed to handle the error appropriately,
       * typically by terminating the program or setting a global error state
       * that the caller is expected to check.
       * Modifying the return value (e.g., returning NULL) would alter the
       * external functionality, which is disallowed by the prompt.
       */
      ill_op ();
      break; /* Added break for clarity, though ill_op() likely terminates */
    }
  return p;
}

/* The operand m may be as above or one of the undocumented
   combinations (ix+d),r and (iy+d),r (if unportable instructions
   are allowed).  */

static const char *
emit_mr (char prefix, char opcode, const char *args)
{
  expressionS arg_m;
  expressionS arg_r;
  const char *current_p;
  char should_emit_instruction = 0;
  char effective_opcode = opcode;

  current_p = parse_exp (args, &arg_m);

  switch (arg_m.X_op)
    {
    case O_md1:
      if (*current_p == ',')
	{
	  const char *p_after_r_parse = parse_exp (current_p + 1, &arg_r);

	  if (arg_r.X_md == 0 &&
	      arg_r.X_op == O_register &&
	      arg_r.X_add_number < 8)
	    {
	      effective_opcode += arg_r.X_add_number - 6;
	      current_p = p_after_r_parse;
	    }
	  else
	    {
	      ill_op ();
	      return current_p;
	    }

	  if (!(ins_ok & INS_Z80N))
	    {
	      check_mach (INS_ROT_II_LD);
	    }
	}
      should_emit_instruction = 1;
      break;

    case O_register:
      should_emit_instruction = 1;
      break;

    default:
      ill_op ();
      break;
    }

  if (should_emit_instruction)
    {
      emit_mx (prefix, effective_opcode, 0, &arg_m);
    }

  return current_p;
}

#define OPCODE_XOR_VALUE 0x46

static void
emit_sx (char prefix, char opcode, expressionS * arg_p)
{
  if (arg_p == NULL)
    {
      ill_op ();
      return;
    }

  switch (arg_p->X_op)
    {
    case O_register:
    case O_md1:
      emit_mx (prefix, opcode, 0, arg_p);
      break;

    default:
      if (arg_p->X_md)
        {
          ill_op ();
          return;
        }
      else
        {
          int buffer_size = prefix ? 2 : 1;
          char *buffer_ptr = frag_more (buffer_size);

          if (buffer_ptr == NULL)
            {
              ill_op ();
              return;
            }

          char *current_pos = buffer_ptr;

          if (prefix)
            {
              *current_pos++ = prefix;
            }

          *current_pos = opcode ^ OPCODE_XOR_VALUE;

          emit_byte (arg_p, BFD_RELOC_8);
        }
      break;
    }
}

/* The operand s may be r, (hl), (ix+d), (iy+d), n.  */
static const char *
emit_s (char prefix, char opcode, const char *args)
{
  expressionS arg_data;
  const char *current_arg_pos;

  current_arg_pos = parse_exp (args, &arg_data);

  const int is_comma_separator = (*current_arg_pos == ',');
  const int is_register_a_direct = (arg_data.X_md == 0 &&
                                    arg_data.X_op == O_register &&
                                    arg_data.X_add_number == REG_A);

  if (is_comma_separator && is_register_a_direct)
    {
      if (!(ins_ok & INS_EZ80) && !sdcc_compat)
        {
          ill_op ();
        }
      
      current_arg_pos++;
      current_arg_pos = parse_exp (current_arg_pos, &arg_data);
    }
  
  emit_sx (prefix, opcode, &arg_data);
  return current_arg_pos;
}

static const char *
emit_sub (char prefix, char opcode, const char *args)
{
  expressionS parsed_expression_details;
  const char *current_parse_position = args;

  if (!(ins_ok & INS_GBZ80)) {
    return emit_s (prefix, opcode, args);
  }

  current_parse_position = parse_exp(current_parse_position, &parsed_expression_details);

  if (parsed_expression_details.X_md != 0 ||
      parsed_expression_details.X_op != O_register ||
      parsed_expression_details.X_add_number != REG_A) {
    ill_op();
  }

  char separator_char_at_pos = *current_parse_position;
  current_parse_position++; 

  if (separator_char_at_pos != ',') {
    error(_("bad instruction syntax"));
    return current_parse_position;
  }

  current_parse_position = parse_exp(current_parse_position, &parsed_expression_details);

  emit_sx (prefix, opcode, &parsed_expression_details);
  return current_parse_position;
}

static const char *
emit_swap (char prefix, char opcode, const char *args)
{
  expressionS reg;
  const char *p;
  char *q;

  if (!(ins_ok & INS_Z80N))
    return emit_mr (prefix, opcode, args);

  p = parse_exp (args, &reg);

  if (!(reg.X_op == O_register && reg.X_add_number == REG_A && reg.X_md == 0))
    {
      ill_op ();
    }

  q = frag_more (2);
  *q++ = 0xED;
  *q = 0x23;
  return p;
}

static const char *
emit_call (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  expressionS addr;
  const char *remaining_args_ptr;

  remaining_args_ptr = parse_exp_not_indexed (args, &addr);

  if (addr.X_md) {
    ill_op ();
  } else {
    char *opcode_output_ptr;

    opcode_output_ptr = frag_more (1);
    *opcode_output_ptr = opcode;
    emit_word (&addr);
  }

  return remaining_args_ptr;
}

/* Operand may be rr, r, (hl), (ix+d), (iy+d).  */
static const char *
emit_incdec (char prefix, char opcode, const char * args)
{
  expressionS operand;
  const char *remaining_args;
  int rnum;

  remaining_args = parse_exp (args, &operand);
  rnum = operand.X_add_number;

  bool is_direct_arith_register = !operand.X_md &&
                                  (operand.X_op == O_register) &&
                                  ((R_ARITH & rnum) != 0);

  if (is_direct_arith_register)
    {
      char *buffer_ptr;
      bool is_indexed_register = ((rnum & R_INDEX) != 0);

      buffer_ptr = frag_more (is_indexed_register ? 2 : 1);

      if (is_indexed_register)
        {
          *buffer_ptr++ = (char)((rnum & R_IX) ? 0xDD : 0xFD);
        }
      
      *buffer_ptr = prefix + (char)((rnum & 0x03) << 4);
    }
  else
    {
      if ((operand.X_op == O_md1) || (operand.X_op == O_register))
        {
          emit_mx (0, opcode, 3, & operand);
        }
      else
        {
          ill_op ();
        }
    }
  return remaining_args;
}

static const char *
emit_jr (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  expressionS addr;
  const char *p;

  p = parse_exp_not_indexed (args, &addr);
  if (addr.X_md)
    {
      ill_op ();
    }
  else
    {
      char *q = frag_more (1);
      *q = opcode;

      expressionS adjusted_addr = addr;
      adjusted_addr.X_add_number--;
      emit_byte (&adjusted_addr, BFD_RELOC_8_PCREL);
    }
  return p;
}

static const char *
emit_jp (char prefix, char opcode, const char * args)
{
  enum {
    JP_IX_PREFIX = 0xDD,
    JP_IY_PREFIX = 0xFD,
    JP_ED_PREFIX = 0xED,
    JP_C_OPCODE = 0x98
  };

  expressionS addr;
  const char *p;
  char *q;

  p = parse_exp_not_indexed (args, &addr);

  if (addr.X_md)
  {
    const int rnum = addr.X_add_number;

    if (addr.X_op == O_register)
    {
      if (REG_HL == (rnum & ~R_INDEX))
      {
        if (rnum & R_INDEX)
        {
          q = frag_more(2);
          *q++ = (rnum & R_IX) ? JP_IX_PREFIX : JP_IY_PREFIX;
        }
        else
        {
          q = frag_more(1);
        }
        *q = prefix;
      }
      else if (rnum == REG_C && (ins_ok & INS_Z80N))
      {
        q = frag_more(2);
        *q++ = JP_ED_PREFIX;
        *q = JP_C_OPCODE;
      }
      else
      {
        ill_op();
      }
    }
    else
    {
      ill_op();
    }
  }
  else
  {
    q = frag_more(1);
    *q = opcode;
    emit_word(&addr);
  }
  return p;
}

static const char *
emit_im (char prefix, char opcode, const char * args)
{
  expressionS mode;
  const char *p;
  char *q;
  int final_offset_value;

  p = parse_exp (args, &mode);

  if (mode.X_md || (mode.X_op != O_constant))
  {
    ill_op ();
  }
  else
  {
    switch (mode.X_add_number)
    {
      case 0:
        final_offset_value = 0;
        break;
      case 1:
        final_offset_value = 2;
        break;
      case 2:
        final_offset_value = 3;
        break;
      default:
        ill_op ();
    }

    q = frag_more (2);
    *q++ = prefix;
    *q = opcode + 8 * final_offset_value;
  }

  return p;
}

static int
is_valid_register_operand(const expressionS *reg_exp)
{
  return (!reg_exp->X_md &&
          reg_exp->X_op == O_register &&
          (reg_exp->X_add_number & R_STACKABLE));
}

static const char *
emit_pop (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  expressionS parsed_reg_exp;
  const char *remaining_args_ptr;
  char *output_byte_ptr;

  remaining_args_ptr = parse_exp (args, &parsed_reg_exp);

  if (is_valid_register_operand(&parsed_reg_exp))
    {
      int register_number = parsed_reg_exp.X_add_number;

      if (register_number & R_INDEX)
	{
	  output_byte_ptr = frag_more (2);
	  *output_byte_ptr++ = (register_number & R_IX) ? 0xDD : 0xFD;
	}
      else
	{
	  output_byte_ptr = frag_more (1);
	}

      *output_byte_ptr = opcode + ((register_number & 3) << 4);
    }
  else
    {
      ill_op ();
    }

  return remaining_args_ptr;
}

static const char *
emit_push (char prefix, char opcode, const char * args)
{
  expressionS arg;
  const char *p;
  char *instruction_start;
  char *operand_address_ptr;

  p = parse_exp (args, & arg);
  if (arg.X_op == O_register)
    return emit_pop (prefix, opcode, args);

  if (arg.X_md) {
    ill_op ();
  } else if (arg.X_op == O_md1) {
    ill_op ();
  } else if (!(ins_ok & INS_Z80N)) {
    ill_op ();
  }

  instruction_start = frag_more (4);

  if (!instruction_start) {
    ill_op ();
    return NULL;
  }

  instruction_start[0] = 0xED;
  instruction_start[1] = 0x8A;

  operand_address_ptr = instruction_start + 2;

  fix_new_exp (frag_now, operand_address_ptr - frag_now->fr_literal, 2, &arg, false,
               BFD_RELOC_Z80_16_BE);

  return p;
}

static const char *
emit_retcc (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  char cc;
  char *q = frag_more(1);

  // Critical: Check if memory allocation failed.
  // Without this check, dereferencing 'q' could lead to a segmentation fault
  // or a write to an invalid memory location, compromising reliability and security.
  if (q == NULL) {
    return NULL; // Indicate a critical internal error.
  }

  const char *p = parse_cc(args, &cc);

  if (p != NULL) {
    // 'args' was successfully parsed, and 'cc' contains the condition code.
    *q = opcode + cc;
  } else {
    // 'args' could not be parsed; use the default 'prefix' value.
    *q = prefix;
  }

  // Return 'p' (remaining arguments) if parsing succeeded,
  // or 'args' (original arguments) if parsing failed.
  // If 'frag_more' failed, we would have already returned NULL.
  return p != NULL ? p : args;
}

static const char *
emit_adc (char prefix, char opcode, const char * args)
{
  expressionS term;
  int rnum;
  const char *p;

  p = parse_exp (args, &term);

  char expected_comma_char = *p;
  p++;
  if (expected_comma_char != ',')
    {
      error (_("bad instruction syntax"));
      return p;
    }

  if ((term.X_md) || (term.X_op != O_register))
    {
      ill_op ();
    }
  else
    {
      switch (term.X_add_number)
        {
        case REG_A:
          p = emit_s (0, prefix, p);
          break;
        case REG_HL:
          p = parse_exp (p, &term);
          if ((!term.X_md) && (term.X_op == O_register))
            {
              rnum = term.X_add_number;
              if (R_ARITH == (rnum & (R_ARITH | R_INDEX)))
                {
                  char *q = frag_more (2);
                  *q++ = 0xED;
                  *q = opcode + ((rnum & 3) << 4);
                  break;
                }
            }
          /* Fall through. */
        default:
          ill_op ();
          break;
        }
    }
  return p;
}

static const char *
emit_add_sp_offset(const char *p)
{
  expressionS term;
  p = parse_exp (p, &term);
  if (!(ins_ok & INS_GBZ80) || term.X_md || term.X_op == O_register) {
    ill_op ();
  }
  char *q = frag_more (1);
  *q = 0xE8;
  emit_byte (&term, BFD_RELOC_Z80_DISP8);
  return p;
}

static const char *
emit_add_indexed_reg_to_reg(const char *p, char opcode, int lhs_reg, int rhs_reg)
{
  char *q;
  q = frag_more ((lhs_reg & R_INDEX) ? 2 : 1);
  if (lhs_reg & R_INDEX) {
    *q++ = (lhs_reg & R_IX) ? 0xDD : 0xFD;
  }
  *q = opcode + ((rhs_reg & 3) << 4);
  return p;
}

static const char *
emit_add_non_indexed_hl_a(const char *p, int lhs_reg)
{
  char *q = frag_more (2);
  *q++ = 0xED;
  *q = 0x33 - (lhs_reg & 3);
  return p;
}

static const char *
emit_add_non_indexed_hl_nn(const char *p, int lhs_reg, const expressionS *operand_term)
{
  char *q = frag_more (2);
  *q++ = 0xED;
  *q = 0x36 - (lhs_reg & 3);
  emit_word (operand_term);
  return p;
}

static const char *
emit_add (char prefix, char opcode, const char * args)
{
  expressionS op1_term;
  const char *p;

  p = parse_exp (args, &op1_term);
  if (*p++ != ',') {
    error (_("bad instruction syntax"));
    return p;
  }

  if (op1_term.X_md || op1_term.X_op != O_register) {
    ill_op ();
  }

  switch (op1_term.X_add_number) {
    case REG_A:
      return emit_s (0, prefix, p);

    case REG_SP:
      return emit_add_sp_offset(p);

    case REG_BC:
    case REG_DE:
      if (!(ins_ok & INS_Z80N)) {
        ill_op();
      }
      // Fall through for common handling with REG_HL, REG_IX, REG_IY
    case REG_HL:
    case REG_IX:
    case REG_IY: {
      expressionS op2_term;
      int lhs_reg = op1_term.X_add_number;

      p = parse_exp(p, &op2_term);

      if (op2_term.X_md != 0 || op2_term.X_op == O_md1) {
        ill_op();
      } else if (op2_term.X_op == O_register) {
        int rhs_reg = op2_term.X_add_number;

        if ((rhs_reg & R_ARITH) && (rhs_reg == lhs_reg || (rhs_reg & ~R_INDEX) != REG_HL)) {
          return emit_add_indexed_reg_to_reg(p, opcode, lhs_reg, rhs_reg);
        } else if (!(lhs_reg & R_INDEX) && (ins_ok & INS_Z80N) && rhs_reg == REG_A) {
          return emit_add_non_indexed_hl_a(p, lhs_reg);
        } else {
          ill_op();
        }
      } else {
        if (!(lhs_reg & R_INDEX) && (ins_ok & INS_Z80N)) {
          return emit_add_non_indexed_hl_nn(p, lhs_reg, &op2_term);
        } else {
          ill_op();
        }
      }
    }

    default:
      ill_op ();
  }
}

static const char *
emit_bit (char prefix, char opcode, const char * args)
{
  static const char BIT_INSTRUCTION_BASE_OPCODE = 0x40;
  static const int MAX_BIT_INDEX = 7;
  static const int BIT_NUMBER_ENCODING_SHIFT = 3;

  expressionS b;
  int bit_number;
  const char *p;

  p = parse_exp (args, &b);

  if (*p != ',') {
    error (_("bad instruction syntax"));
  }
  p++;

  bit_number = b.X_add_number;

  int is_valid_bit_operand = (
      !b.X_md &&
      b.X_op == O_constant &&
      bit_number >= 0 &&
      bit_number <= MAX_BIT_INDEX
  );

  if (is_valid_bit_operand)
    {
      char effective_opcode = opcode + (bit_number << BIT_NUMBER_ENCODING_SHIFT);

      if (opcode == BIT_INSTRUCTION_BASE_OPCODE)
	{
	  p = emit_m (prefix, effective_opcode, p);
	}
      else
	{
          p = emit_mr (prefix, effective_opcode, p);
	}
    }
  else
    {
      ill_op ();
    }

  return p;
}

/* BSLA DE,B; BSRA DE,B; BSRL DE,B; BSRF DE,B; BRLC DE,B (Z80N only) */
static const char *
emit_bshft (char prefix, char opcode, const char * args)
{
  expressionS r1, r2;
  const char *p;
  char *q;

  p = parse_exp (args, & r1);
  if (*p++ != ',')
    error (_("bad instruction syntax"));
  p = parse_exp (p, & r2);

  bool r1_is_de_register = (r1.X_md == 0 && r1.X_op == O_register && r1.X_add_number == REG_DE);
  bool r2_is_b_register = (r2.X_md == 0 && r2.X_op == O_register && r2.X_add_number == REG_B);

  if (!r1_is_de_register || !r2_is_b_register)
    ill_op ();

  q = frag_more (2);
  *q++ = prefix;
  *q = opcode;
  return p;
}

static const char *
emit_jpcc (char prefix, char opcode, const char * args)
{
  char cc;
  const char *return_pointer; // This will hold the pointer to be returned.

  // Attempt to parse a condition code from the input arguments.
  // p_after_cc will point to the character immediately following the parsed CC,
  // or NULL if parsing failed.
  const char *p_after_cc = parse_cc(args, &cc);

  // Check if parse_cc was successful (p_after_cc is not NULL)
  // AND if the character immediately following the parsed CC is a comma.
  if (p_after_cc != NULL && *p_after_cc == ',') {
    // If a comma is present, this means a conditional instruction should be emitted.
    // Advance the pointer past the comma.
    p_after_cc++;
    // Emit a conditional call using the opcode combined with the condition code,
    // and pass the rest of the arguments (starting after the comma).
    return_pointer = emit_call(0, opcode + cc, p_after_cc);
  } else {
    // If parse_cc failed (p_after_cc is NULL) OR no comma followed the CC,
    // then a non-conditional instruction should be emitted.
    // In this case, the original 'args' string is used for the instruction,
    // as the condition code was either not present, invalid, or not properly
    // terminated by a comma for a conditional instruction.
    if (prefix == 0xC3) {
      // If the prefix is 0xC3, emit a JMP instruction (0xE9 is a common JMP opcode).
      return_pointer = emit_jp(0xE9, prefix, args);
    } else {
      // Otherwise, emit a general CALL instruction.
      return_pointer = emit_call(0, prefix, args);
    }
  }

  // Return the pointer to the remaining arguments after processing,
  // or NULL if an error occurred during emission.
  return return_pointer;
}

#define MAX_JR_CC_VALUE (3 << 3)

static const char *
emit_jrcc (char prefix, char opcode, const char * args)
{
  char cc;
  const char *remaining_args;

  const char *cc_end_ptr = parse_cc (args, &cc);

  if (cc_end_ptr != NULL && *cc_end_ptr == ',')
    {
      remaining_args = cc_end_ptr + 1;

      if (cc >= MAX_JR_CC_VALUE)
        {
          error (_("condition code invalid for jr"));
          return NULL;
        }
      else
        {
          return emit_jr (0, opcode + cc, remaining_args);
        }
    }
  else
    {
      return emit_jr (0, prefix, args);
    }
}

static const char *
emit_ex (char prefix_in ATTRIBUTE_UNUSED,
	 char opcode_in ATTRIBUTE_UNUSED, const char * args)
{
  expressionS op_arg1;
  const char *p_current;
  char prefix = 0;
  char opcode = 0;

  p_current = parse_exp_not_indexed (args, &op_arg1);
  p_current = skip_space (p_current);

  if (*p_current != ',')
    {
      error (_("bad instruction syntax"));
      return p_current;
    }
  p_current++;

  if (op_arg1.X_op == O_register)
    {
      if (!op_arg1.X_md)
        {
          switch (op_arg1.X_add_number)
            {
            case REG_AF:
              if (TOLOWER (*p_current) == 'a' && TOLOWER (*(p_current + 1)) == 'f')
                {
                  p_current += 2;
                  if (*p_current == '`')
                    ++p_current;
                  opcode = 0x08;
                }
              break;
            case REG_DE:
              if (TOLOWER (*p_current) == 'h' && TOLOWER (*(p_current + 1)) == 'l')
                {
                  p_current += 2;
                  opcode = 0xEB;
                }
              break;
            }
        }
      else
        {
          if (op_arg1.X_add_number == REG_SP)
            {
              expressionS op_arg2;
              p_current = parse_exp (p_current, &op_arg2);

              if (op_arg2.X_op == O_register
                  && op_arg2.X_md == 0
                  && (op_arg2.X_add_number & ~R_INDEX) == REG_HL)
                {
                  opcode = 0xE3;
                  if (R_INDEX & op_arg2.X_add_number)
                    {
                      prefix = (R_IX & op_arg2.X_add_number) ? 0xDD : 0xFD;
                    }
                }
            }
        }
    }

  if (opcode)
    {
      emit_insn (prefix, opcode, p_current);
    }
  else
    {
      ill_op ();
    }

  return p_current;
}

static const char *
emit_in (char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED,
	const char * args)
{
  expressionS reg_exp, port_exp;
  const char *p_curr_pos;
  char *q_output_ptr;

  // Stage 1: Parse arguments and handle `in (c)` alias.
  // Parse the first expression, which represents the destination register 'reg_exp'.
  p_curr_pos = parse_exp (args, &reg_exp);

  // Check for the special alias: `in (c)` implies `in f,(c)`.
  if (reg_exp.X_md && reg_exp.X_op == O_register && reg_exp.X_add_number == REG_C)
    {
      // `reg_exp` as `(c)` is the port, so store it.
      port_exp = reg_exp;
      // The implicit destination register becomes REG_F.
      reg_exp.X_md = 0;
      reg_exp.X_add_number = REG_F;
    }
  else
    {
      // If not the alias, expect a comma to separate the register and port expressions.
      if (*p_curr_pos++ != ',')
	    {
	      error (_("bad instruction syntax"));
	      return p_curr_pos;
	    }
      // Parse the second expression, which represents the source port 'port_exp'.
      p_curr_pos = parse_exp (p_curr_pos, &port_exp);
    }

  // Stage 2: Validate `reg_exp` and `port_exp` properties.
  // `reg_exp` must be a bare register (not memory-indexed)
  // and its value must be a general-purpose register (0-7) or REG_F.
  if (reg_exp.X_md != 0 || reg_exp.X_op != O_register ||
      (reg_exp.X_add_number > 7 && reg_exp.X_add_number != REG_F))
    {
      ill_op (); // Invalid destination register for IN instruction.
    }

  // `port_exp` must be memory-indexed, meaning it's an address (e.g., (N), (C), (BC)).
  if (!port_exp.X_md)
    {
      ill_op (); // Port operand must be memory-indexed.
    }

  // Stage 3: Emit instruction bytes based on `port_exp`'s characteristics.

  // Case 1: `port_exp` is an immediate value (like `IN A,(N)`).
  if (port_exp.X_op != O_md1 && port_exp.X_op != O_register)
    {
      // For immediate port addresses, only `REG_A` is a valid destination register.
      if (REG_A == reg_exp.X_add_number)
	    {
	      q_output_ptr = frag_more (1); // Allocate 1 byte for the opcode.
	      *q_output_ptr = 0xDB;         // Z80 IN A, (N) opcode.
	      emit_byte (&port_exp, BFD_RELOC_8); // Emit port address as an 8-bit relocation.
	    }
      else
	    {
	      ill_op (); // IN r, (N) is only valid for r=A.
	    }
    }
  // Case 2: `port_exp` is a register (like `IN r,(C)` or `IN r,(BC)`).
  else
    {
      // The port register must be REG_C or REG_BC.
      if (port_exp.X_add_number == REG_C || port_exp.X_add_number == REG_BC)
	    {
          // Specific machine check for `IN r,(BC)`.
	      if (port_exp.X_add_number == REG_BC && !(ins_ok & INS_EZ80))
            {
              ill_op (); // `IN r,(BC)` requires the EZ80 instruction set.
            }
          // Specific machine check for `IN F,(C)`.
	      else if (reg_exp.X_add_number == REG_F && !(ins_ok & (INS_R800|INS_Z80N)))
            {
              check_mach (INS_IN_F_C); // Additional machine check for `IN F,(C)`.
            }

          q_output_ptr = frag_more (2); // Allocate 2 bytes for the ED prefix and opcode.
          *q_output_ptr++ = 0xED;       // Z80 ED prefix for `IN r,(C)` / `IN r,(BC)`.
          // The second byte is 0x40 ORed with (register_number << 3).
          *q_output_ptr = 0x40 | ((reg_exp.X_add_number & 7) << 3);
	    }
	  else
	    {
	      ill_op (); // Invalid register for `IN r,(R)` (only C or BC allowed).
	    }
    }

  return p_curr_pos; // Return the updated parsing position.
}

static const char *
emit_in0 (char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED,
        const char * args)
{
  expressionS reg, port;
  const char *current_parse_pos;
  unsigned char *emitted_bytes_ptr;

  const unsigned char IN_INSTRUCTION_PREFIX = 0xED;
  const unsigned char REG_NUMBER_MAX = 7;
  const unsigned char REG_ENCODING_SHIFT = 3;

  const char *reg_start_pos = args;
  current_parse_pos = parse_exp(reg_start_pos, &reg);

  if (current_parse_pos == reg_start_pos) {
    error(_("invalid register expression"));
    return reg_start_pos;
  }

  char separator_char = *current_parse_pos;
  current_parse_pos++;

  if (separator_char != ',') {
    error(_("bad instruction syntax: expected comma"));
    return current_parse_pos;
  }

  const char *port_start_pos = current_parse_pos;
  current_parse_pos = parse_exp(port_start_pos, &port);

  if (current_parse_pos == port_start_pos) {
    error(_("invalid port expression"));
    return port_start_pos;
  }

  const bool is_valid_register_operand = (reg.X_md == 0
                                         && reg.X_op == O_register
                                         && reg.X_add_number >= 0
                                         && reg.X_add_number <= REG_NUMBER_MAX);

  const bool is_valid_port_operand = (port.X_md != 0
                                      && port.X_op != O_md1
                                      && port.X_op != O_register);

  if (is_valid_register_operand && is_valid_port_operand) {
    emitted_bytes_ptr = (unsigned char *) frag_more(2);

    if (emitted_bytes_ptr == NULL) {
      error(_("out of memory for instruction emission"));
      return current_parse_pos;
    }

    *emitted_bytes_ptr++ = IN_INSTRUCTION_PREFIX;
    *emitted_bytes_ptr = (unsigned char) (reg.X_add_number << REG_ENCODING_SHIFT);

    emit_byte(&port, BFD_RELOC_8);
  } else {
    ill_op();
  }

  return current_parse_pos;
}

static const char *
emit_out (char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED,
	 const char * args)
{
  expressionS reg_exp, port_exp;
  const char *parse_ptr = args;
  char *emit_ptr;

  // 1. Parse the port expression
  parse_ptr = parse_exp (parse_ptr, &port_exp);

  // 2. Expect a comma separator
  if (*parse_ptr++ != ',')
    {
      error (_("bad instruction syntax"));
      return parse_ptr;
    }

  // 3. Parse the register expression
  parse_ptr = parse_exp (parse_ptr, &reg_exp);

  // 4. Validate initial port expression attribute (X_md must be false)
  if (!port_exp.X_md)
    {
      ill_op ();
      return parse_ptr;
    }

  // 5. Handle special case: "out (c), 0"
  // This modifies the register expression before general validation.
  if (reg_exp.X_op == O_constant && reg_exp.X_add_number == 0)
    {
      if (!(ins_ok & INS_Z80N))
	check_mach (INS_OUT_C_0);
      reg_exp.X_op = O_register;
      reg_exp.X_add_number = REG_C; // Map 0 to REG_C for this special case
    }

  // 6. Validate the register operand
  if (reg_exp.X_md             // Register operand should not be a memory address type
      || reg_exp.X_op != O_register // Must be a register
      || reg_exp.X_add_number > 7) // Register index must be within 0-7
    {
      ill_op ();
      return parse_ptr;
    }

  // 7. Determine the instruction form based on the port operand type
  if (port_exp.X_op != O_register && port_exp.X_op != O_md1)
    {
      // Form: OUT (n), A (Port is an immediate value, register must be A)
      if (reg_exp.X_add_number == REG_A)
	{
	  emit_ptr = frag_more (1);
	  *emit_ptr = 0xD3; // Z80 OUT (n), A opcode
	  emit_byte (&port_exp, BFD_RELOC_8); // Emit the 8-bit immediate port value
	}
      else
	{
	  ill_op (); // Register must be A for OUT (n), A instruction
	  return parse_ptr;
	}
    }
  else
    {
      // Form: OUT (C), r or OUT (BC), r (Port is register C or BC)
      if (port_exp.X_add_number == REG_C || port_exp.X_add_number == REG_BC)
	{
	  if (port_exp.X_add_number == REG_BC && !(ins_ok & INS_EZ80))
	    {
	      ill_op (); // OUT (BC), r is EZ80-specific
	      return parse_ptr;
	    }
	  else
	    {
	      emit_ptr = frag_more (2);
	      *emit_ptr++ = 0xED; // Z80 ED prefix
	      *emit_ptr = 0x41 | (reg_exp.X_add_number << 3); // OUT (C), r / OUT (BC), r opcode
	    }
	}
      else
	{
	  ill_op (); // Port register must be C or BC for this instruction form
	  return parse_ptr;
	}
    }

  // Return the pointer to the next part of the instruction string
  return parse_ptr;
}

enum {
  OUT_PREFIX_BYTE = 0xED,
  OUT_SUFFIX_BASE = 0x01,
  REGISTER_SHIFT_AMOUNT = 3,
  MAX_VALID_REGISTER_NUMBER = 7
};

static int is_valid_port_expression_for_out0(const expressionS *port_expr) {
  return port_expr->X_md != 0 &&
         port_expr->X_op != O_register &&
         port_expr->X_op != O_md1;
}

static int is_valid_register_expression_for_out0(const expressionS *reg_expr) {
  return reg_expr->X_md == 0 &&
         reg_expr->X_op == O_register &&
         reg_expr->X_add_number >= 0 &&
         reg_expr->X_add_number <= MAX_VALID_REGISTER_NUMBER;
}

static const char *
emit_out0 (char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED,
         const char * args)
{
  expressionS reg_expr, port_expr;
  const char *parse_ptr;
  char *output_ptr;

  parse_ptr = parse_exp (args, &port_expr);
  if (*parse_ptr++ != ',')
    {
      error (_("bad instruction syntax"));
      return parse_ptr;
    }
  parse_ptr = parse_exp (parse_ptr, &reg_expr);

  if (is_valid_port_expression_for_out0(&port_expr) &&
      is_valid_register_expression_for_out0(&reg_expr))
    {
      output_ptr = frag_more (2);
      *output_ptr++ = OUT_PREFIX_BYTE;
      *output_ptr = OUT_SUFFIX_BASE | (reg_expr.X_add_number << REGISTER_SHIFT_AMOUNT);
      emit_byte (&port_expr, BFD_RELOC_8);
    }
  else
    {
      ill_op ();
    }
  return parse_ptr;
}

static const char *
emit_rst (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  expressionS addr = {0};
  const char *p;
  char *q;

  p = parse_exp_not_indexed (args, &addr);

  if (addr.X_op != O_constant)
    {
      error ("rst needs constant address");
      return p;
    }

  enum { RST_ADDRESS_SHIFT = 3 };
  enum { RST_ADDRESS_VALUE_MASK = (7 << RST_ADDRESS_SHIFT) };

  if ((addr.X_add_number & ~RST_ADDRESS_VALUE_MASK) != 0)
    {
      ill_op ();
    }
  else
    {
      q = frag_more (1);
      *q = opcode + (char)(addr.X_add_number & RST_ADDRESS_VALUE_MASK);
    }
  return p;
}

/* For 8-bit indirect load to memory instructions like: LD (HL),n or LD (ii+d),n.  */
static void
emit_ld_m_n (expressionS *dst, expressionS *src)
{
  char *current_frag_ptr;
  unsigned char instruction_prefix;
  expressionS displacement_expression;

  switch (dst->X_add_number)
    {
    case REG_HL:
      instruction_prefix = 0x00;
      break;
    case REG_IX:
      instruction_prefix = 0xDD;
      break;
    case REG_IY:
      instruction_prefix = 0xFD;
      break;
    default:
      ill_op ();
      return;
    }

  current_frag_ptr = frag_more (instruction_prefix ? 2 : 1);

  if (instruction_prefix)
    {
      *current_frag_ptr++ = instruction_prefix;
    }
  *current_frag_ptr = 0x36;

  if (instruction_prefix)
    {
      displacement_expression = *dst;
      displacement_expression.X_op = O_symbol;
      displacement_expression.X_add_number = 0;
      emit_byte (&displacement_expression, BFD_RELOC_Z80_DISP8);
    }
  emit_byte (src, BFD_RELOC_8);
}

/* For 8-bit load register to memory instructions: LD (<expression>),r.  */
static void
emit_ld_m_r (expressionS *dst, expressionS *src)
{
  char *q;
  unsigned char opcode_prefix; // To hold DD/FD prefix if needed

  // All 'LD (M), R' instructions require R to be a register.
  // This is a common precondition, handle it early.
  if (src->X_op != O_register)
    {
      ill_op();
      return;
    }

  // --- Handle Destination Addressing Modes ---

  // O_md1: Memory Indirect 1 (e.g., (HL+), (HL-), (IX+d), (IY+d))
  if (dst->X_op == O_md1)
    {
      if (ins_ok & INS_GBZ80)
        {
          // GBZ80 specific: LD (HL+),A or LD (HL-),A
          if (src->X_add_number != REG_A)
            {
              ill_op();
              return;
            }
          q = frag_more(1);
          // Assuming REG_HL maps to HL+ (0x22) and other values to HL- (0x32)
          *q = (dst->X_add_number == REG_HL) ? 0x22 : 0x32;
          return;
        }
      else
        {
          // Z80 specific: LD (IX+d),r or LD (IY+d),r
          if (dst->X_add_number == REG_IX)
            opcode_prefix = 0xDD;
          else if (dst->X_add_number == REG_IY)
            opcode_prefix = 0xFD;
          else
            {
              // O_md1 for Z80 should only be IX/IY with displacement.
              // Other O_md1 forms are not supported here for Z80.
              ill_op();
              return;
            }

          // Source register must be a general-purpose register (B,C,D,E,H,L,(HL),A) (0-7)
          if (src->X_add_number > 7)
            {
              ill_op();
              return;
            }

          // Emit prefix byte, then the main opcode byte
          q = frag_more(2); // Two bytes for opcode (prefix + main opcode)
          *q++ = opcode_prefix;
          *q = 0x70 | src->X_add_number; // LD (ii+d),r opcode

          // Handle displacement byte, reusing dst for offset
          expressionS dst_offset_exp = *dst; // Create a copy
          dst_offset_exp.X_op = O_symbol; // Modify as per original logic
          dst_offset_exp.X_add_number = 0; // Clear add_number as per original logic
          emit_byte(&dst_offset_exp, BFD_RELOC_Z80_DISP8);
          return;
        }
    }

  // O_register: Memory Register (e.g., (BC), (DE), (HL))
  if (dst->X_op == O_register)
    {
      switch (dst->X_add_number)
        {
        case REG_BC:
        case REG_DE:
          // LD (BC),A or LD (DE),A
          if (src->X_add_number != REG_A)
            {
              ill_op();
              return;
            }
          q = frag_more(1);
          *q = 0x02 | ((dst->X_add_number & 3) << 4); // Opcode based on register pair
          return;

        case REG_HL:
          // LD (HL),r
          // Source register must be a general-purpose register (0-7)
          if (src->X_add_number > 7)
            {
              ill_op();
              return;
            }
          q = frag_more(1);
          *q = 0x70 | src->X_add_number; // LD (HL),r opcode
          return;

        case REG_IX:
        case REG_IY:
          // If dst->X_op is O_register and dst->X_add_number is REG_IX/IY,
          // it typically refers to (IX+d),r or (IY+d),r and is handled by the O_md1 path.
          // Direct (IX),r or (IY),r are usually treated as (IX+0),r, also covered by O_md1.
          // For O_register directly without a prefix from O_md1, this case is not supported.
          ill_op();
          return;

        default:
          ill_op(); // Unsupported register for O_register addressing mode
          return;
        }
    }

  // Default case: Destination is a direct address (e.g., O_symbol, O_absolute)
  // This covers `LD (nn),A` for any dst->X_op not handled above.
  if (src->X_add_number != REG_A)
    {
      ill_op();
      return;
    }
  q = frag_more(1);
  *q = (ins_ok & INS_GBZ80) ? 0xEA : 0x32;
  emit_word(dst); // Emit 16-bit address
  return;
}

/* For 16-bit load register to memory instructions: LD (<expression>),rr.  */
static void
emit_ld_m_rr (expressionS *dst, expressionS *src)
{
  int prefix_val = 0;
  int opcode_val = 0;
  int emit_prefix = 0;

  if (dst->X_op == O_md1 || dst->X_op == O_register)
    {
      if (!(ins_ok & INS_EZ80))
        {
          ill_op();
          return;
        }

      switch (dst->X_add_number)
        {
        case REG_IX: prefix_val = 0xDD; emit_prefix = 1; break;
        case REG_IY: prefix_val = 0xFD; emit_prefix = 1; break;
        case REG_HL: prefix_val = 0xED; emit_prefix = 1; break;
        default:
          ill_op();
          return;
        }

      switch (src->X_add_number)
        {
        case REG_BC: opcode_val = 0x0F; break;
        case REG_DE: opcode_val = 0x1F; break;
        case REG_HL: opcode_val = 0x2F; break;
        case REG_IX: opcode_val = (prefix_val != 0xFD) ? 0x3F : 0x3E; break;
        case REG_IY: opcode_val = (prefix_val != 0xFD) ? 0x3E : 0x3F; break;
        default:
          ill_op();
          return;
        }

      char *q = frag_more(2);
      *q++ = prefix_val;
      *q = opcode_val;

      if (prefix_val == 0xFD || prefix_val == 0xDD)
        {
          expressionS dst_offset = *dst;
          dst_offset.X_op = O_symbol;
          dst_offset.X_add_number = 0;
          emit_byte(&dst_offset, BFD_RELOC_Z80_DISP8);
        }
    }
  else
    {
      if (ins_ok & INS_GBZ80)
        {
          if (src->X_add_number == REG_SP)
            {
              prefix_val = 0x00;
              opcode_val = 0x08;
              emit_prefix = 0;
            }
          else
            {
              ill_op();
              return;
            }
        }
      else
        {
          switch (src->X_add_number)
            {
            case REG_BC: prefix_val = 0xED; opcode_val = 0x43; emit_prefix = 1; break;
            case REG_DE: prefix_val = 0xED; opcode_val = 0x53; emit_prefix = 1; break;
            case REG_HL: prefix_val = 0x00; opcode_val = 0x22; emit_prefix = 0; break;
            case REG_IX: prefix_val = 0xDD; opcode_val = 0x22; emit_prefix = 1; break;
            case REG_IY: prefix_val = 0xFD; opcode_val = 0x22; emit_prefix = 1; break;
            case REG_SP: prefix_val = 0xED; opcode_val = 0x73; emit_prefix = 1; break;
            default:
              ill_op();
              return;
            }
        }

      char *q = frag_more(emit_prefix ? 2 : 1);
      if (emit_prefix)
        *q++ = prefix_val;
      *q = opcode_val;

      emit_word(dst);
    }
}

static void
emit_ld_r_m (expressionS *dst, expressionS *src)
{
  char opcode_byte = 0;
  char prefix_byte = 0;
  char *p;

  if (dst->X_op == O_register && dst->X_add_number == REG_A && src->X_op == O_register)
    {
      switch (src->X_add_number)
        {
        case REG_BC: opcode_byte = 0x0A; break;
        case REG_DE: opcode_byte = 0x1A; break;
        default: break;
        }
      if (opcode_byte != 0)
        {
          p = frag_more (1);
          *p = opcode_byte;
          return;
        }
    }

  switch (src->X_op)
    {
    case O_md1:
      if (ins_ok & INS_GBZ80)
        {
          if (dst->X_op == O_register && dst->X_add_number == REG_A)
            {
              opcode_byte = (src->X_add_number == REG_HL) ? 0x2A : 0x3A;
              p = frag_more (1);
              *p = opcode_byte;
              return;
            }
          else
            {
              ill_op ();
              return;
            }
        }
    case O_register:
      if (dst->X_op != O_register || dst->X_add_number > 7)
        {
          ill_op ();
          return;
        }

      opcode_byte = 0x46 | ((dst->X_add_number & 7) << 3);

      switch (src->X_add_number)
        {
        case REG_HL: prefix_byte = 0x00; break;
        case REG_IX: prefix_byte = 0xDD; break;
        case REG_IY: prefix_byte = 0xFD; break;
        default:
          ill_op ();
          return;
        }

      p = frag_more (prefix_byte ? 2 : 1);
      if (prefix_byte)
        {
          *p++ = prefix_byte;
        }
      *p = opcode_byte;

      if (prefix_byte)
        {
          expressionS disp_src_expr = *src;
          disp_src_expr.X_op = O_symbol;
          disp_src_expr.X_add_number = 0;
          emit_byte (&disp_src_expr, BFD_RELOC_Z80_DISP8);
        }
      break;

    default:
      if (dst->X_op == O_register && dst->X_add_number == REG_A)
        {
          p = frag_more (1);
          *p = (ins_ok & INS_GBZ80) ? 0xFA : 0x3A;
          emit_word (src);
        }
      else
        {
          ill_op ();
        }
      break;
    }
}

static void
emit_ld_r_n (expressionS *dst, expressionS *src)
{
  char *opcode_ptr;
  unsigned char prefix_byte = 0;
  unsigned int destination_register_value = dst->X_add_number;

  switch (destination_register_value)
    {
    case REG_H | R_IX:
    case REG_L | R_IX:
      prefix_byte = 0xDD;
      break;

    case REG_H | R_IY:
    case REG_L | R_IY:
      prefix_byte = 0xFD;
      break;

    case REG_A:
    case REG_B:
    case REG_C:
    case REG_D:
    case REG_E:
    case REG_H:
    case REG_L:
      break;

    default:
      ill_op ();
      return;
    }

  opcode_ptr = frag_more (prefix_byte ? 2 : 1);

  if (prefix_byte)
    {
      if (ins_ok & INS_GBZ80)
        ill_op ();
      else if (!(ins_ok & (INS_EZ80 | INS_R800 | INS_Z80N)))
        check_mach (INS_IDX_HALF);

      *opcode_ptr++ = prefix_byte;
    }

  *opcode_ptr = 0x06 | ((destination_register_value & 0x07) << 3);

  emit_byte (src, BFD_RELOC_8);
}

static int is_gp_8bit_reg(int reg_num) {
    return (reg_num >= REG_B && reg_num <= REG_L) || reg_num == REG_A;
}

static int is_ix_half(int reg_num) {
    return (reg_num == (REG_H | R_IX) || reg_num == (REG_L | R_IX));
}

static int is_iy_half(int reg_num) {
    return (reg_num == (REG_H | R_IY) || reg_num == (REG_L | R_IY));
}

static int get_base_reg_for_opcode(int reg_num) {
    if (is_ix_half(reg_num) || is_iy_half(reg_num)) {
        return reg_num & 7;
    }
    return reg_num;
}

static void
emit_ld_r_r (expressionS *dst, expressionS *src)
{
  char *q;
  int prefix = 0;
  int opcode = -1;
  int ii_halves = 0;

  int dst_reg = dst->X_add_number;
  int src_reg = src->X_add_number;

  if (dst_reg == REG_SP)
    {
      opcode = 0xF9;
      if (src_reg == REG_HL) { prefix = 0x00; }
      else if (src_reg == REG_IX) { prefix = 0xDD; }
      else if (src_reg == REG_IY) { prefix = 0xFD; }
      else { ill_op (); return; }
    }
  else if (dst_reg == REG_HL && src_reg == REG_I)
    {
      if (!(ins_ok & INS_EZ80)) { ill_op (); return; }
      if (cpu_mode < 1) { error (_("ADL mode instruction")); return; }
      prefix = 0xED;
      opcode = 0xD7;
    }
  else if (dst_reg == REG_I && src_reg == REG_HL)
    {
      if (!(ins_ok & INS_EZ80)) { ill_op (); return; }
      if (cpu_mode < 1) { error (_("ADL mode instruction")); return; }
      prefix = 0xED;
      opcode = 0xC7;
    }
  else if (dst_reg == REG_I && src_reg == REG_A)
    {
      prefix = 0xED;
      opcode = 0x47;
    }
  else if (dst_reg == REG_MB && src_reg == REG_A)
    {
      if (!(ins_ok & INS_EZ80)) { ill_op (); return; }
      if (cpu_mode < 1) { error (_("ADL mode instruction")); return; }
      prefix = 0xED;
      opcode = 0x6D;
    }
  else if (dst_reg == REG_R && src_reg == REG_A)
    {
      prefix = 0xED;
      opcode = 0x4F;
    }
  else if (dst_reg == REG_A && src_reg == REG_I)
    {
      prefix = 0xED;
      opcode = 0x57;
    }
  else if (dst_reg == REG_A && src_reg == REG_R)
    {
      prefix = 0xED;
      opcode = 0x5F;
    }
  else if (dst_reg == REG_A && src_reg == REG_MB)
    {
      if (!(ins_ok & INS_EZ80)) { ill_op (); return; }
      if (cpu_mode < 1) { error (_("ADL mode instruction")); return; }
      prefix = 0xED;
      opcode = 0x6E;
    }
  else
    {
      int temp_prefix = 0;
      int temp_ii_halves = 0;

      int dst_is_ixh = is_ix_half(dst_reg);
      int dst_is_iyh = is_iy_half(dst_reg);
      int src_is_ixh = is_ix_half(src_reg);
      int src_is_iyh = is_iy_half(src_reg);

      if (dst_is_ixh || src_is_ixh) {
          temp_prefix = 0xDD;
          temp_ii_halves = 1;
      }
      if (dst_is_iyh || src_is_iyh) {
          if (temp_prefix == 0xDD) { ill_op(); return; }
          temp_prefix = 0xFD;
          temp_ii_halves = 1;
      }
      
      prefix = temp_prefix;
      ii_halves = temp_ii_halves;

      int dst_is_gp = is_gp_8bit_reg(dst_reg);
      int src_is_gp = is_gp_8bit_reg(src_reg);

      if (!(dst_is_gp || dst_is_ixh || dst_is_iyh)) { ill_op(); return; }
      if (!(src_is_gp || src_is_ixh || src_is_iyh)) { ill_op(); return; }

      if (ii_halves) {
          if ((dst_is_gp && (dst_reg == REG_H || dst_reg == REG_L) && (src_is_ixh || src_is_iyh)) ||
              (src_is_gp && (src_reg == REG_H || src_reg == REG_L) && (dst_is_ixh || dst_is_iyh)))
          {
              ill_op(); return;
          }
      } else {
          if (!dst_is_gp || !src_is_gp) { ill_op(); return; }
      }

      int actual_dst_reg_for_opcode = get_base_reg_for_opcode(dst_reg);
      int actual_src_reg_for_opcode = get_base_reg_for_opcode(src_reg);
      opcode = 0x40 + ((actual_dst_reg_for_opcode & 7) << 3) + (actual_src_reg_for_opcode & 7);
    }

  if (opcode == -1) { ill_op(); return; }

  if ((ins_ok & INS_GBZ80) && prefix != 0) { ill_op (); return; }

  if (ii_halves && !(ins_ok & (INS_EZ80|INS_R800|INS_Z80N))) {
    check_mach (INS_IDX_HALF);
  }

  if (prefix == 0 && (ins_ok & INS_EZ80))
    {
      switch (opcode)
        {
        case 0x40:
        case 0x49:
        case 0x52:
        case 0x5B:
          as_warn (_("unsupported instruction, assembled as NOP"));
          opcode = 0x00;
          break;
        default:;
        }
    }
  
  q = frag_more (prefix ? 2 : 1);
  if (prefix)
    *q++ = prefix;
  *q = opcode;
}

#define Z80_PREFIX_ED 0xED
#define Z80_PREFIX_DD 0xDD
#define Z80_PREFIX_FD 0xFD

#define EZ80_OP_LD_BC_IND_REG 0x07
#define EZ80_OP_LD_DE_IND_REG 0x17
#define EZ80_OP_LD_HL_IND_REG 0x27
#define EZ80_OP_LD_IXIY_SAME_OR_HL_TARGET 0x37
#define EZ80_OP_LD_IXIY_CROSS_TARGET 0x31

#define Z80_OP_LD_BC_IND_NN 0x4B
#define Z80_OP_LD_DE_IND_NN 0x5B
#define Z80_OP_LD_HL_IND_NN 0x2A
#define Z80_OP_LD_SP_IND_NN 0x7B
#define Z80_OP_LD_IXIY_IND_NN 0x2A

static void
emit_ld_rr_m (expressionS *dst, expressionS *src)
{
  char *q;
  int prefix = 0;
  int opcode = 0;
  expressionS src_offset_expr;

  if (ins_ok & INS_GBZ80)
    {
      ill_op ();
      return;
    }

  switch (src->X_op)
    {
    case O_md1:
    case O_register:
      if (!(ins_ok & INS_EZ80))
        {
          ill_op ();
          return;
        }

      if (src->X_op == O_md1)
        {
          prefix = (src->X_add_number == REG_IX) ? Z80_PREFIX_DD : Z80_PREFIX_FD;
        }
      else
        {
          prefix = Z80_PREFIX_ED;
        }

      switch (dst->X_add_number)
        {
        case REG_BC: opcode = EZ80_OP_LD_BC_IND_REG; break;
        case REG_DE: opcode = EZ80_OP_LD_DE_IND_REG; break;
        case REG_HL: opcode = EZ80_OP_LD_HL_IND_REG; break;
        case REG_IX:
          opcode = (prefix == Z80_PREFIX_ED || prefix == Z80_PREFIX_DD) ? EZ80_OP_LD_IXIY_SAME_OR_HL_TARGET : EZ80_OP_LD_IXIY_CROSS_TARGET;
          break;
        case REG_IY:
          opcode = (prefix == Z80_PREFIX_ED || prefix == Z80_PREFIX_DD) ? EZ80_OP_LD_IXIY_CROSS_TARGET : EZ80_OP_LD_IXIY_SAME_OR_HL_TARGET;
          break;
        default:
          ill_op ();
          return;
        }

      q = frag_more (2);
      *q++ = prefix;
      *q = opcode;

      if (prefix != Z80_PREFIX_ED)
        {
          src_offset_expr = *src;
          src_offset_expr.X_op = O_symbol;
          src_offset_expr.X_add_number = 0;
          emit_byte (&src_offset_expr, BFD_RELOC_Z80_DISP8);
        }
      break;

    default:
      switch (dst->X_add_number)
        {
        case REG_BC: prefix = Z80_PREFIX_ED; opcode = Z80_OP_LD_BC_IND_NN; break;
        case REG_DE: prefix = Z80_PREFIX_ED; opcode = Z80_OP_LD_DE_IND_NN; break;
        case REG_HL: prefix = 0x00;          opcode = Z80_OP_LD_HL_IND_NN; break;
        case REG_SP: prefix = Z80_PREFIX_ED; opcode = Z80_OP_LD_SP_IND_NN; break;
        case REG_IX: prefix = Z80_PREFIX_DD; opcode = Z80_OP_LD_IXIY_IND_NN; break;
        case REG_IY: prefix = Z80_PREFIX_FD; opcode = Z80_OP_LD_IXIY_IND_NN; break;
        default:
          ill_op ();
          return;
        }

      q = frag_more (prefix ? 2 : 1);
      if (prefix)
        *q++ = prefix;
      *q = opcode;
      emit_word (src);
      break;
    }
}

static void
emit_ld_rr_nn (expressionS *dst, expressionS *src)
{
  char *q;
  int prefix = 0x00;
  int opcode;

  switch (dst->X_add_number)
    {
    case REG_IX:
      prefix = 0xDD;
      opcode = 0x21;
      break;
    case REG_IY:
      prefix = 0xFD;
      opcode = 0x21;
      break;
    case REG_BC:
    case REG_DE:
    case REG_HL:
    case REG_SP:
      opcode = 0x01 + ((dst->X_add_number & 0x03) << 4);
      break;
    default:
      ill_op ();
      return;
    }

  if (prefix != 0x00 && (ins_ok & INS_GBZ80))
    {
      ill_op ();
      return;
    }

  q = frag_more (prefix != 0x00 ? 2 : 1);

  if (prefix != 0x00)
    {
      *q++ = prefix;
    }

  *q = opcode;

  emit_word (src);
}

static void handle_ld_memory_dest(expressionS *dst, expressionS *src) {
    if (src->X_op == O_register) {
        if (src->X_add_number <= 7) {
            emit_ld_m_r(dst, src);
        } else {
            emit_ld_m_rr(dst, src);
        }
    } else {
        emit_ld_m_n(dst, src);
    }
}

static void handle_ld_register_dest(expressionS *dst, expressionS *src) {
    if (src->X_md) {
        if (dst->X_add_number <= 7) {
            emit_ld_r_m(dst, src);
        } else {
            emit_ld_rr_m(dst, src);
        }
    } else if (src->X_op == O_register) {
        emit_ld_r_r(dst, src);
    } else {
        if ((dst->X_add_number & ~R_INDEX) <= 7) {
            emit_ld_r_n(dst, src);
        } else {
            emit_ld_rr_nn(dst, src);
        }
    }
}

static const char *
emit_ld (char prefix_in ATTRIBUTE_UNUSED, char opcode_in ATTRIBUTE_UNUSED,
	const char * args)
{
  expressionS dst, src;
  const char *p;

  p = parse_exp (args, & dst);
  if (*p++ != ',') {
    error (_("bad instruction syntax"));
  }
  p = parse_exp (p, & src);

  if (dst.X_md) {
    handle_ld_memory_dest(&dst, &src);
  } else if (dst.X_op == O_register) {
    handle_ld_register_dest(&dst, &src);
  } else {
    ill_op();
  }

  return p;
}

static int is_dst_mem_hl(const expressionS *exp) {
  return exp->X_md == 0 && exp->X_add_number == REG_HL;
}

static int is_src_reg_a(const expressionS *exp) {
  return exp->X_md == 0 && exp->X_add_number == REG_A;
}

static int is_dst_reg_a(const expressionS *exp) {
  return exp->X_md == 0 && exp->X_add_number == REG_A;
}

static int is_src_mem_hl(const expressionS *exp) {
  return exp->X_md != 0 && exp->X_add_number == REG_HL;
}

static const char *
emit_lddldi (char prefix, char opcode, const char * args)
{
  expressionS dst, src;
  const char *p;
  char *q;

  if (!(ins_ok & INS_GBZ80)) {
    return emit_insn (prefix, opcode, args);
  }

  p = parse_exp (args, &dst);
  if (*p++ != ',') {
    error (_("bad instruction syntax"));
  }
  p = parse_exp (p, &src);

  if (dst.X_op != O_register || src.X_op != O_register) {
    ill_op ();
  }

  opcode = (opcode & 0x08) ? 0x32 : 0x22;

  if (is_dst_mem_hl(&dst) && is_src_reg_a(&src)) {
    
  } else if (is_dst_reg_a(&dst) && is_src_mem_hl(&src)) {
    opcode |= 0x08;
  } else {
    ill_op ();
  }

  q = frag_more (1);
  *q = opcode;
  return p;
}

static const char *
emit_ldh (char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED,
        const char * args)
{
  expressionS dst, src;
  const char *p;

  enum {
    LDH_AR_MEM_OPCODE = 0xF0, /* LDH A, (addr) */
    LDH_AR_CR_OPCODE  = 0xF2, /* LDH A, C      */
    LDH_MEM_AR_OPCODE = 0xE0, /* LDH (addr), A */
    LDH_CR_AR_OPCODE  = 0xE2  /* LDH (C), A    */
  };

  p = parse_exp (args, &dst);
  if (*p++ != ',')
    {
      error (_("bad instruction syntax"));
      return p;
    }

  p = parse_exp (p, &src);

  /* Helper functions to clarify expression checks */
  /* Checks if operand is direct register A */
  static inline int is_reg_a_direct(const expressionS *exp) {
    return exp->X_md == 0 && exp->X_op == O_register && exp->X_add_number == REG_A;
  }

  /* Checks if operand is direct register C */
  static inline int is_reg_c_direct(const expressionS *exp) {
    return exp->X_md == 0 && exp->X_op == O_register && exp->X_add_number == REG_C;
  }

  /* Checks if operand is a generic memory addressing mode (X_md != 0, X_op != O_md1) */
  static inline int is_generic_memory_addressing(const expressionS *exp) {
    return exp->X_md != 0 && exp->X_op != O_md1;
  }

  /* Checks if operand is a direct memory address (X_md != 0, X_op != O_md1, and not a register indirect) */
  static inline int is_direct_memory_address(const expressionS *exp) {
    return exp->X_md != 0 && exp->X_op != O_md1 && exp->X_op != O_register;
  }

  /* Checks if operand is register C used as an indirect memory address (X_md != 0, X_op == O_register, X_add_number == REG_C) */
  static inline int is_reg_c_indirect_address(const expressionS *exp) {
    return exp->X_md != 0 && exp->X_op == O_register && exp->X_add_number == REG_C;
  }

  /* Helper to emit a single byte opcode */
  static inline void emit_opcode_byte(unsigned char byte_val) {
    *frag_more(1) = byte_val;
  }

  /* Logic simplified using helper functions */

  /* Case: LDH A, src */
  if (is_reg_a_direct(&dst))
    {
      if (is_direct_memory_address(&src))
        {
          emit_opcode_byte(LDH_AR_MEM_OPCODE); /* 0xF0: LDH A, (addr) */
          emit_byte(&src, BFD_RELOC_8);
        }
      else if (is_reg_c_direct(&src))
        {
          emit_opcode_byte(LDH_AR_CR_OPCODE); /* 0xF2: LDH A, C */
        }
      else
        {
          /* Handles cases where dst is REG_A, but src is neither (addr) nor C. */
          /* Original code's fall-through behavior implies this is an ill_op. */
          ill_op();
        }
    }
  /* Case: LDH dst, A */
  else if (is_reg_a_direct(&src))
    {
      if (is_reg_c_indirect_address(&dst))
        {
          emit_opcode_byte(LDH_CR_AR_OPCODE); /* 0xE2: LDH (C), A */
        }
      else if (is_direct_memory_address(&dst))
        {
          emit_opcode_byte(LDH_MEM_AR_OPCODE); /* 0xE0: LDH (addr), A */
          emit_byte(&dst, BFD_RELOC_8);
        }
      else
        {
          /* Handles cases where src is REG_A, but dst is neither (C) nor (addr). */
          /* Original code's behavior implies this is an ill_op. */
          ill_op();
        }
    }
  /* All other unhandled combinations */
  else
    {
      ill_op();
    }

  return p;
}

static const char *
emit_ldhl (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  expressionS dst_exp, src_exp;
  const char *current_pos = args;

  current_pos = parse_exp (current_pos, &dst_exp);

  if (*current_pos != ',')
    {
      error (_("bad instruction syntax"));
      return current_pos;
    }
  current_pos++;

  current_pos = parse_exp (current_pos, &src_exp);

  const int is_dst_sp = !dst_exp.X_md && (dst_exp.X_op == O_register) && (dst_exp.X_add_number == REG_SP);
  const int is_src_valid_displacement = !src_exp.X_md && (src_exp.X_op != O_register) && (src_exp.X_op != O_md1);

  if (!is_dst_sp || !is_src_valid_displacement)
    {
      ill_op ();
    }

  char *output_ptr = frag_more (1);
  *output_ptr = opcode;
  emit_byte (&src_exp, BFD_RELOC_Z80_DISP8);

  return current_pos;
}

static const char *
parse_lea_pea_args (const char * args, expressionS *op)
{
  if (args == NULL || op == NULL) {
    return NULL;
  }

  const char *current_pos = parse_exp(args, op);

  if (current_pos == NULL) {
    return NULL;
  }

  int is_sdcc_compat_mode = sdcc_compat;
  int has_comma_delimiter = (*current_pos == ',');
  int is_register_operand = (op->X_op == O_register);

  if (is_sdcc_compat_mode && has_comma_delimiter && is_register_operand)
    {
      expressionS offset_expr;

      const char *next_expr_start = current_pos + 1;
      const char *next_parse_end = parse_exp(next_expr_start, &offset_expr);

      if (next_parse_end == NULL) {
        return NULL;
      }

      op->X_op = O_add;

      expressionS *symbol_from_offset = make_expr_symbol(&offset_expr);
      if (symbol_from_offset == NULL) {
        return NULL;
      }
      op->X_add_symbol = symbol_from_offset;

      current_pos = next_parse_end;
    }

  return current_pos;
}

static const char *
emit_lea (char prefix, char opcode_in, const char * args)
{
  expressionS dst, src;
  const char *p;
  char final_opcode;
  int dst_rnum;
  int src_rnum;

  p = parse_exp (args, &dst);
  if (dst.X_md != 0 || dst.X_op != O_register)
    ill_op ();

  dst_rnum = dst.X_add_number;

  switch (dst_rnum)
    {
    case REG_BC:
    case REG_DE:
    case REG_HL:
      final_opcode = 0x02 | ((dst_rnum & 0x03) << 4);
      break;
    case REG_IX:
      final_opcode = 0x32;
      break;
    case REG_IY:
      final_opcode = 0x33;
      break;
    default:
      ill_op ();
    }

  if (*p++ != ',')
    error (_("bad instruction syntax"));

  p = parse_lea_pea_args (p, &src);

  if (src.X_md != 0)
    ill_op ();

  switch (src.X_op)
    {
    case O_add:
      break;
    case O_register:
      src.X_add_symbol = zero;
      break;
    default:
      ill_op ();
    }

  src_rnum = src.X_add_number;

  if (dst_rnum == REG_IX && src_rnum == REG_IY)
    {
      final_opcode = 0x54;
    }
  else if (dst_rnum == REG_IY && src_rnum == REG_IX)
    {
      final_opcode = 0x55;
    }

  char *q = frag_more (2);
  *q++ = prefix;
  *q = final_opcode;

  src.X_op = O_symbol;
  src.X_add_number = 0;
  emit_byte (&src, BFD_RELOC_Z80_DISP8);

  return p;
}

#define MLT_INSTRUCTION_BYTE_COUNT 2

#define Z80N_PREFIX_OPCODE 0xED
#define Z80N_MLT_OPCODE 0x30

#define REGISTER_ENCODING_MASK 3
#define REGISTER_ENCODING_SHIFT 4

static const char *
emit_mlt (char prefix, char opcode, const char * args)
{
  expressionS arg = {0};
  const char *p_remainder;
  char *q_instruction_bytes;

  p_remainder = parse_exp (args, &arg);

  if (arg.X_md != 0 || arg.X_op != O_register || !(arg.X_add_number & R_ARITH))
    {
      ill_op ();
    }

  q_instruction_bytes = frag_more (MLT_INSTRUCTION_BYTE_COUNT);

  if (q_instruction_bytes == NULL)
    {
      ill_op();
    }

  if (ins_ok & INS_Z80N)
    {
      if (arg.X_add_number != REG_DE)
	{
	  ill_op ();
	}
      *q_instruction_bytes++ = Z80N_PREFIX_OPCODE;
      *q_instruction_bytes = Z80N_MLT_OPCODE;
    }
  else
    {
      *q_instruction_bytes++ = prefix;
      *q_instruction_bytes = opcode | ((arg.X_add_number & REGISTER_ENCODING_MASK) << REGISTER_ENCODING_SHIFT);
    }

  return p_remainder;
}

/* MUL D,E (Z80N only) */
static bool is_valid_register_operand(const expressionS *operand, int expected_register_id)
{
  return operand->X_md == 0 &&
         operand->X_op == O_register &&
         operand->X_add_number == expected_register_id;
}

static const char *
emit_mul (char prefix, char opcode, const char * args)
{
  expressionS r1_operand;
  expressionS r2_operand;
  const char *current_pos = args;

  current_pos = parse_exp (current_pos, &r1_operand);

  if (*current_pos != ',')
    error (_("bad instruction syntax"));
  current_pos++;

  current_pos = parse_exp (current_pos, &r2_operand);

  if (!is_valid_register_operand(&r1_operand, REG_D) ||
      !is_valid_register_operand(&r2_operand, REG_E))
    ill_op ();

  char *output_ptr = frag_more (2);
  *output_ptr++ = prefix;
  *output_ptr = opcode;

  return current_pos;
}

static bool is_invalid_operand_base_type(const expressionS *exp) {
  return exp->X_md != 0 || exp->X_op == O_md1;
}

static const char *
emit_nextreg (char prefix, char opcode ATTRIBUTE_UNUSED, const char * args)
{
  expressionS rr = {0};
  expressionS nn = {0};
  const char *p = args;
  char *q;

  p = parse_exp (p, &rr);

  if (*p != ',') {
    error (_("bad instruction syntax: expected ',' between operands"));
  }
  p++;

  p = parse_exp (p, &nn);

  if (is_invalid_operand_base_type(&rr) || rr.X_op == O_register || is_invalid_operand_base_type(&nn)) {
    ill_op ();
  }

  q = frag_more (2);

  *q++ = prefix;

  emit_byte (&rr, BFD_RELOC_8);

  if (nn.X_op == O_register) {
    if (nn.X_add_number == REG_A) {
      *q = 0x92;
    } else {
      ill_op ();
    }
  } else {
    *q = 0x91;
    emit_byte (&nn, BFD_RELOC_8);
  }

  return p;
}

static const char *
emit_pea (char prefix, char opcode, const char * args)
{
  expressionS arg;
  const char *remaining_args_ptr;
  char *output_buffer_ptr;

  remaining_args_ptr = parse_lea_pea_args (args, &arg);

  if (arg.X_md != 0 || arg.X_op != O_add || !(arg.X_add_number & R_INDEX)) {
    ill_op ();
  }

  output_buffer_ptr = frag_more (2);
  if (output_buffer_ptr == NULL) {
    ill_op ();
  }

  *output_buffer_ptr++ = prefix;
  *output_buffer_ptr = opcode + (arg.X_add_number == REG_IY ? 1 : 0);

  arg.X_op = O_symbol;
  arg.X_add_number = 0;
  emit_byte (&arg, BFD_RELOC_Z80_DISP8);

  return remaining_args_ptr;
}

static const char *
emit_reti (char prefix, char opcode, const char * args)
{
  static const char GBZ80_PREFIX_NONE = 0x00;
  static const char GBZ80_RETI_OPCODE = 0xD9;

  if (ins_ok & INS_GBZ80)
  {
    return emit_insn (GBZ80_PREFIX_NONE, GBZ80_RETI_OPCODE, args);
  }

  return emit_insn (prefix, opcode, args);
}

#define TST_HL_INDIRECT_RNUM 6

static const char *
emit_tst (char prefix, char opcode, const char *args)
{
  expressionS final_arg_s;
  expressionS initial_arg_s;
  const char *p_current;
  char *output_ptr;

  p_current = parse_exp (args, &initial_arg_s);

  if (*p_current == ',' &&
      initial_arg_s.X_md == 0 &&
      initial_arg_s.X_op == O_register &&
      initial_arg_s.X_add_number == REG_A)
    {
      if (!(ins_ok & INS_EZ80))
        {
          ill_op ();
        }
      p_current = parse_exp (p_current + 1, &final_arg_s);
    }
  else
    {
      final_arg_s = initial_arg_s;
    }

  int rnum_val = 0;

  switch (final_arg_s.X_op)
    {
    case O_md1:
      ill_op ();
      break;

    case O_register:
      if (final_arg_s.X_md != 0)
        {
          if (final_arg_s.X_add_number != REG_HL)
            {
              ill_op ();
            }
          rnum_val = TST_HL_INDIRECT_RNUM;
        }
      else
        {
          rnum_val = final_arg_s.X_add_number;
        }

      output_ptr = frag_more (2);
      *output_ptr++ = prefix;
      *output_ptr = opcode | (rnum_val << 3);
      break;

    default:
      if (final_arg_s.X_md)
        {
          ill_op ();
        }

      output_ptr = frag_more (2);
      if (ins_ok & INS_Z80N)
	{
	  *output_ptr++ = 0xED;
	  *output_ptr = 0x27;
	}
      else
	{
	  *output_ptr++ = prefix;
	  *output_ptr = opcode | 0x60;
	}
      emit_byte (&final_arg_s, BFD_RELOC_8);
    }
  return p_current;
}

static const char *
emit_insn_n (char prefix, char opcode, const char *args)
{
  static const int INSTRUCTION_HEADER_SIZE = 2;

  expressionS arg;
  const char *remaining_args;

  remaining_args = parse_exp(args, &arg);

  if (arg.X_md || arg.X_op == O_register || arg.X_op == O_md1)
  {
    ill_op();
  }

  char *instruction_ptr = frag_more(INSTRUCTION_HEADER_SIZE);

  *instruction_ptr++ = prefix;
  *instruction_ptr = opcode;

  emit_byte(&arg, BFD_RELOC_8);

  return remaining_args;
}

static void
emit_data (int size ATTRIBUTE_UNUSED)
{
  const char *current_ptr;
  char *output_ptr;
  char quote_char;
  int item_length;
  expressionS exp;

  if (is_it_end_of_statement ())
    {
      demand_empty_rest_of_line ();
      return;
    }

  current_ptr = skip_space (input_line_pointer);

  for (;;)
    {
      if (*current_ptr == '\"' || *current_ptr == '\'')
        {
          quote_char = *current_ptr;
          const char *string_start = ++current_ptr;

          item_length = 0;
          while (*current_ptr != '\0' && *current_ptr != quote_char)
            {
              ++current_ptr;
              ++item_length;
            }

          output_ptr = frag_more (item_length);
          memcpy (output_ptr, string_start, item_length);

          if (*current_ptr == '\0')
            {
              as_warn (_("unterminated string"));
              // current_ptr is at '\0', no further advancement for a closing quote.
            }
          else
            {
              // Found closing quote, advance past it.
              ++current_ptr;
            }
        }
      else
        {
          current_ptr = parse_exp (current_ptr, &exp);
          if (exp.X_op == O_md1 || exp.X_op == O_register)
            {
              ill_op ();
              // Critical error, stop processing any further items on this line.
              break;
            }
          if (exp.X_md)
            {
              as_warn (_("parentheses ignored"));
            }
          emit_byte (&exp, BFD_RELOC_8);
        }

      // After processing an item (string or expression), skip any trailing spaces.
      current_ptr = skip_space (current_ptr);

      // Check if there's a comma to continue processing more items.
      if (*current_ptr == ',')
        {
          // Found a comma, advance past it and any following spaces for the next item.
          ++current_ptr;
          current_ptr = skip_space (current_ptr);
        }
      else
        {
          // No comma, so no more items to process in this statement.
          break;
        }
    }

  // Update the global input_line_pointer to the first character that was not consumed.
  input_line_pointer = (char *) current_ptr;
}

static void
z80_cons (int size)
{
  const char *current_char_ptr;
  expressionS exp_data;

  if (is_it_end_of_statement ())
    {
      demand_empty_rest_of_line ();
      return;
    }

  current_char_ptr = skip_space (input_line_pointer);

  do
    {
      current_char_ptr = parse_exp (current_char_ptr, &exp_data);

      if (exp_data.X_op == O_md1 || exp_data.X_op == O_register)
	{
	  ill_op ();
	  break;
	}

      if (exp_data.X_md)
	{
	  as_warn (_("parentheses ignored"));
	}

      emit_data_val (&exp_data, size);
      current_char_ptr = skip_space (current_char_ptr);

  } while (*current_char_ptr++ == ',');

  input_line_pointer = (char *)(current_char_ptr - 1);
}

/* next functions were commented out because it is difficult to mix
   both ADL and Z80 mode instructions within one COFF file:
   objdump cannot recognize point of mode switching.
*/
static void
set_cpu_mode (int mode)
{
  if (ins_ok & INS_EZ80)
  {
    cpu_mode = mode;
  }
  else
  {
    error (_("CPU mode is unsupported by target"));
  }
}

static void
assume (int arg ATTRIBUTE_UNUSED)
{
  char *symbol_name;
  char char_after_symbol;
  int value;

  input_line_pointer = skip_space(input_line_pointer);
  char_after_symbol = get_symbol_name(&symbol_name);

  if (strcasecmp(symbol_name, "ADL") != 0)
    {
      ill_op();
      return;
    }

  restore_line_pointer(char_after_symbol);
  input_line_pointer = skip_space(input_line_pointer);

  if (*input_line_pointer != '=')
    {
      error(_("assignment expected"));
      return;
    }
  input_line_pointer++; 

  input_line_pointer = skip_space(input_line_pointer);
  value = get_single_number();

  set_cpu_mode(value);
}

static const char *
emit_mulub (char prefix, char opcode, const char * args)
{
  const char *p = skip_space (args);

  // Check for the mandatory "a," prefix.
  if (TOLOWER (*p++) != 'a' || *p++ != ',')
    {
      ill_op ();
    }
  else
    {
      char reg_char = TOLOWER (*p++);

      // Validate the register character (must be 'b', 'c', 'd', or 'e').
      if (reg_char < 'b' || reg_char > 'e')
        {
          ill_op ();
        }

      // After the register, there should be no more arguments.
      if (*skip_space (p))
        {
          ill_op ();
        }

      // If all checks pass, proceed with emitting the instruction.
      check_mach (INS_R800); // Ensure the current machine supports R800 instructions.

      char *q = frag_more (2); // Allocate 2 bytes for the instruction.
      *q++ = prefix;           // Write the instruction prefix.
      // Write the opcode, incorporating the register value.
      // Registers 'b' through 'e' map to 0 through 3, left-shifted by 3 bits.
      *q = opcode + ((reg_char - 'b') << 3);
    }
  return p;
}

static const char *
emit_muluw (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  const char *p_current = skip_space (args);

  if (TOLOWER (*p_current) != 'h')
    {
      ill_op ();
    }
  p_current++;

  if (TOLOWER (*p_current) != 'l')
    {
      ill_op ();
    }
  p_current++;

  if (*p_current != ',')
    {
      ill_op ();
    }
  p_current++;

  const char *p = p_current;

  expressionS reg;
  p = parse_exp (p, & reg);

  if (!reg.X_md && reg.X_op == O_register)
    {
      int register_number = reg.X_add_number;

      if (register_number == REG_BC || register_number == REG_SP)
        {
          check_mach (INS_R800);
          char *q = frag_more (2);

          *q++ = prefix;
          *q = opcode + ((register_number & 3) << 4);
        }
      else
        {
          ill_op ();
        }
    }
  else
    {
      ill_op ();
    }

  return p;
}

enum SuffixType {
    SUFFIX_IL = 0,
    SUFFIX_IS = 1,
    SUFFIX_L = 2,
    SUFFIX_LIL = 3,
    SUFFIX_LIS = 4,
    SUFFIX_S = 5,
    SUFFIX_SIL = 6,
    SUFFIX_SIS = 7
};

enum InstCode {
    INST_CODE_S_IS = 0x40,
    INST_CODE_L_IS = 0x49,
    INST_CODE_S_IL = 0x52,
    INST_CODE_L_IL = 0x5B
};

static int
assemble_suffix (const char **suffix_ptr)
{
  static const char suffix_strings[8][4] =
    {
      "il",
      "is",
      "l",
      "lil",
      "lis",
      "s",
      "sil",
      "sis"
    };

  const char *current_pos = *suffix_ptr;
  char sbuf[4];
  int parsed_char_count = 0;
  enum InstCode effective_inst_code;

  if (*current_pos != '.')
    return 0;
  current_pos++;

  for (; (parsed_char_count < 3) && ISALPHA(*current_pos); parsed_char_count++)
    sbuf[parsed_char_count] = TOLOWER(*current_pos++);
  sbuf[parsed_char_count] = '\0';

  if (*current_pos != '\0' && !is_whitespace(*current_pos))
    return 0;

  *suffix_ptr = current_pos;

  const char (*found_suffix_entry)[4];
  found_suffix_entry = bsearch (sbuf, suffix_strings, ARRAY_SIZE(suffix_strings), sizeof(suffix_strings[0]), (int(*)(const void*, const void*)) strcmp);

  if (found_suffix_entry == NULL)
    return 0;

  enum SuffixType suffix_type = (enum SuffixType)(found_suffix_entry - suffix_strings);

  switch (suffix_type)
    {
      case SUFFIX_IL:
        effective_inst_code = cpu_mode ? INST_CODE_L_IL : INST_CODE_S_IL;
        break;
      case SUFFIX_IS:
        effective_inst_code = cpu_mode ? INST_CODE_L_IS : INST_CODE_S_IS;
        break;
      case SUFFIX_L:
        effective_inst_code = cpu_mode ? INST_CODE_L_IL : INST_CODE_L_IS;
        break;
      case SUFFIX_LIL:
        effective_inst_code = INST_CODE_L_IL;
        break;
      case SUFFIX_LIS:
        effective_inst_code = INST_CODE_L_IS;
        break;
      case SUFFIX_S:
        effective_inst_code = cpu_mode ? INST_CODE_S_IL : INST_CODE_S_IS;
        break;
      case SUFFIX_SIL:
        effective_inst_code = INST_CODE_S_IL;
        break;
      case SUFFIX_SIS:
        effective_inst_code = INST_CODE_S_IS;
        break;
    }

  *frag_more(1) = effective_inst_code;

  switch (effective_inst_code)
    {
      case INST_CODE_S_IS:
        inst_mode = INST_MODE_FORCED | INST_MODE_S | INST_MODE_IS;
        break;
      case INST_CODE_L_IS:
        inst_mode = INST_MODE_FORCED | INST_MODE_L | INST_MODE_IS;
        break;
      case INST_CODE_S_IL:
        inst_mode = INST_MODE_FORCED | INST_MODE_S | INST_MODE_IL;
        break;
      case INST_CODE_L_IL:
        inst_mode = INST_MODE_FORCED | INST_MODE_L | INST_MODE_IL;
        break;
    }

  return 1;
}

static void
psect (int arg)
{
#if defined(OBJ_ELF)
  obj_elf_section (arg);
#elif defined(OBJ_COFF)
  obj_coff_section (arg);
#else
#error Unknown object format
#endif
}

static void
set_inss (int inss)
{
  if (!sdcc_compat) {
    as_fatal(_("Invalid directive"));
  }

  int initial_ins_ok_value = ins_ok;

  ins_ok &= INS_MARCH_MASK;
  ins_ok |= inss;

  if (initial_ins_ok_value != ins_ok) {
    cpu_mode = 0;
  }
}

void ignore_rest_of_line(void);

static void
ignore (int arg)
{
  (void)arg;
  ignore_rest_of_line ();
}

static void
area (int arg)
{
  if (!sdcc_compat) {
    as_fatal (_("Invalid directive"));
  }

  char *p = input_line_pointer;
  for (; *p != '\0' && *p != '(' && *p != '\n'; p++) {
    /* Scan for '(', '\n', or end of string */
  }

  char original_char_at_p = *p;

  /* Temporarily terminate the string at `p` if it's an opening parenthesis.
   * This ensures `psect` always processes the prefix correctly.
   * If `*p` is already `\n` or `\0`, it acts as a natural terminator. */
  if (original_char_at_p == '(') {
    *p = '\n';
  }

  psect(arg); /* Process the line segment up to the effective terminator. */

  /* Restore the character if it was temporarily modified and handle post-processing. */
  if (original_char_at_p == '(') {
    *p = original_char_at_p; /* Restore '(' */
    p++;                     /* Advance local pointer, mirroring original `*p++ = '(';` */
    ignore_rest_of_line();   /* Handle the rest of the line after the parenthesis. */
  }
}

/* Port specific pseudo ops.  */
const pseudo_typeS md_pseudo_table[] =
{
  { ".area", area, 0},
  { ".assume", assume, 0},
  { ".ez80", set_inss, INS_EZ80},
  { ".gbz80", set_inss, INS_GBZ80},
  { ".module", ignore, 0},
  { ".optsdcc", ignore, 0},
  { ".r800", set_inss, INS_R800},
  { ".set", s_set, 0},
  { ".z180", set_inss, INS_Z180},
  { ".hd64", set_inss, INS_Z180},
  { ".z80", set_inss, INS_Z80},
  { ".z80n", set_inss, INS_Z80N},
  { "db" , emit_data, 1},
  { "d24", z80_cons, 3},
  { "d32", z80_cons, 4},
  { "def24", z80_cons, 3},
  { "def32", z80_cons, 4},
  { "defb", emit_data, 1},
  { "defm", emit_data, 1},
  { "defs", s_space, 1}, /* Synonym for ds on some assemblers.  */
  { "defw", z80_cons, 2},
  { "ds",   s_space, 1}, /* Fill with bytes rather than words.  */
  { "dw", z80_cons, 2},
  { "psect", psect, 0}, /* TODO: Translate attributes.  */
  { "set", 0, 0}, 		/* Real instruction on z80.  */
  { "xdef", s_globl, 0},	/* Synonym for .GLOBAL */
  { "xref", s_ignore, 0},	/* Synonym for .EXTERN */
  { NULL, 0, 0 }
} ;

static table_t instab[] =
{
  { "adc",  0x88, 0x4A, emit_adc,  INS_ALL },
  { "add",  0x80, 0x09, emit_add,  INS_ALL },
  { "and",  0x00, 0xA0, emit_s,    INS_ALL },
  { "bit",  0xCB, 0x40, emit_bit,  INS_ALL },
  { "brlc", 0xED, 0x2C, emit_bshft,INS_Z80N },
  { "bsla", 0xED, 0x28, emit_bshft,INS_Z80N },
  { "bsra", 0xED, 0x29, emit_bshft,INS_Z80N },
  { "bsrf", 0xED, 0x2B, emit_bshft,INS_Z80N },
  { "bsrl", 0xED, 0x2A, emit_bshft,INS_Z80N },
  { "call", 0xCD, 0xC4, emit_jpcc, INS_ALL },
  { "ccf",  0x00, 0x3F, emit_insn, INS_ALL },
  { "cp",   0x00, 0xB8, emit_s,    INS_ALL },
  { "cpd",  0xED, 0xA9, emit_insn, INS_NOT_GBZ80 },
  { "cpdr", 0xED, 0xB9, emit_insn, INS_NOT_GBZ80 },
  { "cpi",  0xED, 0xA1, emit_insn, INS_NOT_GBZ80 },
  { "cpir", 0xED, 0xB1, emit_insn, INS_NOT_GBZ80 },
  { "cpl",  0x00, 0x2F, emit_insn, INS_ALL },
  { "daa",  0x00, 0x27, emit_insn, INS_ALL },
  { "dec",  0x0B, 0x05, emit_incdec,INS_ALL },
  { "di",   0x00, 0xF3, emit_insn, INS_ALL },
  { "djnz", 0x00, 0x10, emit_jr,   INS_NOT_GBZ80 },
  { "ei",   0x00, 0xFB, emit_insn, INS_ALL },
  { "ex",   0x00, 0x00, emit_ex,   INS_NOT_GBZ80 },
  { "exx",  0x00, 0xD9, emit_insn, INS_NOT_GBZ80 },
  { "halt", 0x00, 0x76, emit_insn, INS_ALL },
  { "im",   0xED, 0x46, emit_im,   INS_NOT_GBZ80 },
  { "in",   0x00, 0x00, emit_in,   INS_NOT_GBZ80 },
  { "in0",  0xED, 0x00, emit_in0,  INS_Z180|INS_EZ80 },
  { "inc",  0x03, 0x04, emit_incdec,INS_ALL },
  { "ind",  0xED, 0xAA, emit_insn, INS_NOT_GBZ80 },
  { "ind2", 0xED, 0x8C, emit_insn, INS_EZ80 },
  { "ind2r",0xED, 0x9C, emit_insn, INS_EZ80 },
  { "indm", 0xED, 0x8A, emit_insn, INS_EZ80 },
  { "indmr",0xED, 0x9A, emit_insn, INS_EZ80 },
  { "indr", 0xED, 0xBA, emit_insn, INS_NOT_GBZ80 },
  { "indrx",0xED, 0xCA, emit_insn, INS_EZ80 },
  { "ini",  0xED, 0xA2, emit_insn, INS_NOT_GBZ80 },
  { "ini2", 0xED, 0x84, emit_insn, INS_EZ80 },
  { "ini2r",0xED, 0x94, emit_insn, INS_EZ80 },
  { "inim", 0xED, 0x82, emit_insn, INS_EZ80 },
  { "inimr",0xED, 0x92, emit_insn, INS_EZ80 },
  { "inir", 0xED, 0xB2, emit_insn, INS_NOT_GBZ80 },
  { "inirx",0xED, 0xC2, emit_insn, INS_EZ80 },
  { "jp",   0xC3, 0xC2, emit_jpcc, INS_ALL },
  { "jr",   0x18, 0x20, emit_jrcc, INS_ALL },
  { "ld",   0x00, 0x00, emit_ld,   INS_ALL },
  { "ldd",  0xED, 0xA8, emit_lddldi,INS_ALL }, /* GBZ80 has special meaning */
  { "lddr", 0xED, 0xB8, emit_insn, INS_NOT_GBZ80 },
  { "lddrx",0xED, 0xBC, emit_insn, INS_Z80N },
  { "lddx", 0xED, 0xAC, emit_insn, INS_Z80N },
  { "ldh",  0xE0, 0x00, emit_ldh,  INS_GBZ80 },
  { "ldhl", 0x00, 0xF8, emit_ldhl, INS_GBZ80 },
  { "ldi",  0xED, 0xA0, emit_lddldi,INS_ALL }, /* GBZ80 has special meaning */
  { "ldir", 0xED, 0xB0, emit_insn, INS_NOT_GBZ80 },
  { "ldirx",0xED, 0xB4, emit_insn, INS_Z80N },
  { "ldix", 0xED, 0xA4, emit_insn, INS_Z80N },
  { "ldpirx",0xED,0xB7, emit_insn, INS_Z80N },
  { "ldws", 0xED, 0xA5, emit_insn, INS_Z80N },
  { "lea",  0xED, 0x02, emit_lea,  INS_EZ80 },
  { "mirror",0xED,0x24, emit_insn, INS_Z80N },
  { "mlt",  0xED, 0x4C, emit_mlt,  INS_Z180|INS_EZ80|INS_Z80N },
  { "mul",  0xED, 0x30, emit_mul,  INS_Z80N },
  { "mulub",0xED, 0xC5, emit_mulub,INS_R800 },
  { "muluw",0xED, 0xC3, emit_muluw,INS_R800 },
  { "neg",  0xED, 0x44, emit_insn, INS_NOT_GBZ80 },
  { "nextreg",0xED,0x91,emit_nextreg,INS_Z80N },
  { "nop",  0x00, 0x00, emit_insn, INS_ALL },
  { "or",   0x00, 0xB0, emit_s,    INS_ALL },
  { "otd2r",0xED, 0xBC, emit_insn, INS_EZ80 },
  { "otdm", 0xED, 0x8B, emit_insn, INS_Z180|INS_EZ80 },
  { "otdmr",0xED, 0x9B, emit_insn, INS_Z180|INS_EZ80 },
  { "otdr", 0xED, 0xBB, emit_insn, INS_NOT_GBZ80 },
  { "otdrx",0xED, 0xCB, emit_insn, INS_EZ80 },
  { "oti2r",0xED, 0xB4, emit_insn, INS_EZ80 },
  { "otim", 0xED, 0x83, emit_insn, INS_Z180|INS_EZ80 },
  { "otimr",0xED, 0x93, emit_insn, INS_Z180|INS_EZ80 },
  { "otir", 0xED, 0xB3, emit_insn, INS_NOT_GBZ80 },
  { "otirx",0xED, 0xC3, emit_insn, INS_EZ80 },
  { "out",  0x00, 0x00, emit_out,  INS_NOT_GBZ80 },
  { "out0", 0xED, 0x01, emit_out0, INS_Z180|INS_EZ80 },
  { "outd", 0xED, 0xAB, emit_insn, INS_NOT_GBZ80 },
  { "outd2",0xED, 0xAC, emit_insn, INS_EZ80 },
  { "outi", 0xED, 0xA3, emit_insn, INS_NOT_GBZ80 },
  { "outi2",0xED, 0xA4, emit_insn, INS_EZ80 },
  { "outinb",0xED,0x90, emit_insn, INS_Z80N },
  { "pea",  0xED, 0x65, emit_pea,  INS_EZ80 },
  { "pixelad",0xED,0x94,emit_insn, INS_Z80N },
  { "pixeldn",0xED,0x93,emit_insn, INS_Z80N },
  { "pop",  0x00, 0xC1, emit_pop,  INS_ALL },
  { "push", 0x00, 0xC5, emit_push, INS_ALL },
  { "res",  0xCB, 0x80, emit_bit,  INS_ALL },
  { "ret",  0xC9, 0xC0, emit_retcc,INS_ALL },
  { "reti", 0xED, 0x4D, emit_reti, INS_ALL }, /*GBZ80 has its own opcode for it*/
  { "retn", 0xED, 0x45, emit_insn, INS_NOT_GBZ80 },
  { "rl",   0xCB, 0x10, emit_mr,   INS_ALL },
  { "rla",  0x00, 0x17, emit_insn, INS_ALL },
  { "rlc",  0xCB, 0x00, emit_mr,   INS_ALL },
  { "rlca", 0x00, 0x07, emit_insn, INS_ALL },
  { "rld",  0xED, 0x6F, emit_insn, INS_NOT_GBZ80 },
  { "rr",   0xCB, 0x18, emit_mr,   INS_ALL },
  { "rra",  0x00, 0x1F, emit_insn, INS_ALL },
  { "rrc",  0xCB, 0x08, emit_mr,   INS_ALL },
  { "rrca", 0x00, 0x0F, emit_insn, INS_ALL },
  { "rrd",  0xED, 0x67, emit_insn, INS_NOT_GBZ80 },
  { "rsmix",0xED, 0x7E, emit_insn, INS_EZ80 },
  { "rst",  0x00, 0xC7, emit_rst,  INS_ALL },
  { "sbc",  0x98, 0x42, emit_adc,  INS_ALL },
  { "scf",  0x00, 0x37, emit_insn, INS_ALL },
  { "set",  0xCB, 0xC0, emit_bit,  INS_ALL },
  { "setae",0xED, 0x95, emit_insn, INS_Z80N },
  { "sl1",  0xCB, 0x30, emit_mr,   INS_SLI|INS_Z80N },
  { "sla",  0xCB, 0x20, emit_mr,   INS_ALL },
  { "sli",  0xCB, 0x30, emit_mr,   INS_SLI|INS_Z80N },
  { "sll",  0xCB, 0x30, emit_mr,   INS_SLI|INS_Z80N },
  { "slp",  0xED, 0x76, emit_insn, INS_Z180|INS_EZ80 },
  { "sra",  0xCB, 0x28, emit_mr,   INS_ALL },
  { "srl",  0xCB, 0x38, emit_mr,   INS_ALL },
  { "stmix",0xED, 0x7D, emit_insn, INS_EZ80 },
  { "stop", 0x00, 0x10, emit_insn, INS_GBZ80 },
  { "sub",  0x00, 0x90, emit_sub,  INS_ALL },
  { "swap", 0xCB, 0x30, emit_swap, INS_GBZ80|INS_Z80N },
  { "swapnib",0xED,0x23,emit_insn, INS_Z80N },
  { "test", 0xED, 0x27, emit_insn_n, INS_Z80N },
  { "tst",  0xED, 0x04, emit_tst,  INS_Z180|INS_EZ80|INS_Z80N },
  { "tstio",0xED, 0x74, emit_insn_n,INS_Z180|INS_EZ80 },
  { "xor",  0x00, 0xA8, emit_s,    INS_ALL },
} ;

void
md_assemble (char *str)
{
  const char *p;
  char *old_ptr;
  int i;
  table_t *insp;
  bool continue_processing = true;

  err_flag = 0;
  inst_mode = cpu_mode ? (INST_MODE_L | INST_MODE_IL) : (INST_MODE_S | INST_MODE_IS);
  old_ptr = input_line_pointer;

  p = skip_space (str);

  size_t buf_copy_limit = BUFLEN - 1;
  for (i = 0; (i < buf_copy_limit) && (ISALPHA (*p) || ISDIGIT (*p)); ++i) {
    buf[i] = TOLOWER (*p++);
  }
  buf[i] = 0;

  if (i == buf_copy_limit && (ISALPHA(*p) || ISDIGIT(*p))) {
    buf[BUFLEN - 3] = '.';
    buf[BUFLEN - 2] = '.';
    as_bad (_("Unknown instruction '%s'"), buf);
    continue_processing = false;
  }

  if (continue_processing) {
    dwarf2_emit_insn (0);

    if ((*p) && !is_whitespace (*p)) {
      if (*p != '.' || !(ins_ok & INS_EZ80) || !assemble_suffix (&p)) {
        as_bad (_("syntax error"));
        continue_processing = false;
      }
    }
  }

  if (continue_processing) {
    p = skip_space (p);
    key = buf;

    insp = bsearch (&key, instab, ARRAY_SIZE (instab),
                    sizeof (instab[0]), key_cmp);

    if (!insp || (insp->inss && !(insp->inss & ins_ok))) {
      *frag_more (1) = 0;
      as_bad (_("Unknown instruction `%s'"), buf);
    } else {
      p = insp->fp (insp->prefix, insp->opcode, p);
      p = skip_space (p);
      if ((!err_flag) && *p) {
        as_bad (_("junk at end of line, "
                  "first unrecognized character is `%c'"), *p);
      }
    }
  }

  input_line_pointer = old_ptr;
}

static int
signed_overflow (signed long value, unsigned bitsize)
{
  if (bitsize == 0) {
    return 1;
  }

  const unsigned max_shift_bits_for_ull = sizeof(unsigned long long) * CHAR_BIT;

  if (bitsize >= max_shift_bits_for_ull) {
    return 0;
  }
  
  unsigned long long target_max_positive_val_ull = (1ULL << (bitsize - 1)) - 1;
  unsigned long long magnitude_of_min_val_ull = (1ULL << (bitsize - 1));

  signed long long upper_bound = (signed long long)target_max_positive_val_ull;
  signed long long lower_bound = -(signed long long)magnitude_of_min_val_ull;

  return value < lower_bound || value > upper_bound;
}

static int
unsigned_overflow (unsigned long value, unsigned bitsize)
{
  if (bitsize == 0) {
    return value != 0;
  }

  if (bitsize >= (unsigned int)(sizeof(unsigned long) * CHAR_BIT)) {
    return 0;
  }

  unsigned long threshold = 1UL << bitsize;
  return value >= threshold;
}

static bool
is_overflow (long value, unsigned bitsize)
{
  if (bitsize == 0)
    return true;
  if (value < 0)
    return signed_overflow (value, bitsize);
  return unsigned_overflow (value, bitsize);
}

static void
report_signed_reloc_overflow (fixS *fixP, long val, int bits, int done_flag)
{
  if (done_flag && signed_overflow (val, bits))
    as_bad_where (fixP->fx_file, fixP->fx_line,
                  _("%d-bit signed offset out of range (%+ld)"), bits, val);
}

static void
report_unsigned_reloc_overflow (fixS *fixP, long val, int bits, int done_flag)
{
  if (done_flag && is_overflow (val, bits))
    as_warn_where (fixP->fx_file, fixP->fx_line,
                   _("%d-bit overflow (%+ld)"), bits, val);
}

void
md_apply_fix (fixS * fixP, valueT* valP, segT seg)
{
  long val = *valP;
  char *p_lit = fixP->fx_where + fixP->fx_frag->fr_literal;

  if (fixP->fx_addsy == NULL)
    {
      fixP->fx_done = 1;
    }
  else if (fixP->fx_pcrel)
    {
      segT s = S_GET_SEGMENT (fixP->fx_addsy);
      if (s == seg || s == absolute_section)
	{
	  val += S_GET_VALUE (fixP->fx_addsy);
	  fixP->fx_done = 1;
	}
    }

  fixP->fx_no_overflow = 1; // Default: no overflow check needed or performed for this relocation type.

  switch (fixP->fx_r_type)
    {
    case BFD_RELOC_8_PCREL:
    case BFD_RELOC_Z80_DISP8:
      report_signed_reloc_overflow (fixP, val, 8, fixP->fx_done);
      *p_lit++ = (char)val;
      fixP->fx_no_overflow = 0; // Overflow check was performed for these types.
      break;

    case BFD_RELOC_Z80_BYTE0:
      *p_lit++ = (char)val;
      break;

    case BFD_RELOC_Z80_BYTE1:
      *p_lit++ = (char)(val >> 8);
      break;

    case BFD_RELOC_Z80_BYTE2:
      *p_lit++ = (char)(val >> 16);
      break;

    case BFD_RELOC_Z80_BYTE3:
      *p_lit++ = (char)(val >> 24);
      break;

    case BFD_RELOC_8:
      report_unsigned_reloc_overflow (fixP, val, 8, fixP->fx_done);
      *p_lit++ = (char)val;
      fixP->fx_no_overflow = 0; // Overflow check was performed for this type.
      break;

    case BFD_RELOC_Z80_WORD1: // Big-endian word (bytes 2 and 3)
      *p_lit++ = (char)(val >> 16);
      *p_lit++ = (char)(val >> 24);
      break;

    case BFD_RELOC_Z80_WORD0: // Little-endian word (bytes 0 and 1)
      *p_lit++ = (char)val;
      *p_lit++ = (char)(val >> 8);
      break;

    case BFD_RELOC_16: // Little-endian 16-bit
      report_unsigned_reloc_overflow (fixP, val, 16, fixP->fx_done);
      *p_lit++ = (char)val;
      *p_lit++ = (char)(val >> 8);
      fixP->fx_no_overflow = 0; // Overflow check was performed for this type.
      break;

    case BFD_RELOC_24: // Little-endian 24-bit
      report_unsigned_reloc_overflow (fixP, val, 24, fixP->fx_done);
      *p_lit++ = (char)val;
      *p_lit++ = (char)(val >> 8);
      *p_lit++ = (char)(val >> 16);
      fixP->fx_no_overflow = 0; // Overflow check was performed for this type.
      break;

    case BFD_RELOC_32: // Little-endian 32-bit
      report_unsigned_reloc_overflow (fixP, val, 32, fixP->fx_done);
      *p_lit++ = (char)val;
      *p_lit++ = (char)(val >> 8);
      *p_lit++ = (char)(val >> 16);
      *p_lit++ = (char)(val >> 24);
      fixP->fx_no_overflow = 0; // Overflow check was performed for this type.
      break;

    case BFD_RELOC_Z80_16_BE: // Big-endian 16-bit
      // Note: Original code implies overflow can happen (fx_no_overflow = 0)
      // but does not explicitly perform an overflow check within this function.
      // This behavior is maintained to preserve external functionality.
      *p_lit++ = (char)(val >> 8);
      *p_lit++ = (char)val;
      fixP->fx_no_overflow = 0;
      break;

    default:
      printf (_("md_apply_fix: unknown reloc type 0x%x\n"), fixP->fx_r_type);
      abort ();
    }
}

/* GAS will call this to generate a reloc.  GAS will pass the
   resulting reloc to `bfd_install_relocation'.  This currently works
   poorly, as `bfd_install_relocation' often does the wrong thing, and
   instances of `tc_gen_reloc' have been written to work around the
   problems, which in turns makes it difficult to fix
   `bfd_install_relocation'.  */

/* If while processing a fixup, a reloc really
   needs to be created then it is done here.  */

arelent *
tc_gen_reloc (asection *seg ATTRIBUTE_UNUSED , fixS *fixp)
{
  arelent *reloc;

  if (fixp->fx_subsy != NULL)
    {
      as_bad_subtract (fixp);
      return NULL;
    }

  reloc = notes_alloc (sizeof (arelent));
  if (reloc == NULL)
    {
      return NULL;
    }

  reloc->sym_ptr_ptr = notes_alloc (sizeof (asymbol *));
  if (reloc->sym_ptr_ptr == NULL)
    {
      return NULL;
    }

  *reloc->sym_ptr_ptr = symbol_get_bfdsym (fixp->fx_addsy);
  reloc->address = fixp->fx_frag->fr_address + fixp->fx_where;
  reloc->addend = fixp->fx_offset;
  reloc->howto = bfd_reloc_type_lookup (stdoutput, fixp->fx_r_type);
  if (reloc->howto == NULL)
    {
      as_bad_where (fixp->fx_file, fixp->fx_line,
		    _("reloc %d not supported by object file format"),
		    (int) fixp->fx_r_type);
      return NULL;
    }

  if (fixp->fx_r_type == BFD_RELOC_VTABLE_INHERIT
      || fixp->fx_r_type == BFD_RELOC_VTABLE_ENTRY)
    reloc->address = fixp->fx_offset;

  return reloc;
}

static int colonless_labels = 0;

int
z80_tc_labels_without_colon (void)
{
  return colonless_labels;
}

#include <string.h>

int
z80_tc_label_is_local (const char *name)
{
  if (local_label_prefix == NULL) {
    return 0;
  }
  if (name == NULL) {
    return 0;
  }
  size_t prefix_len = strlen(local_label_prefix);
  if (prefix_len == 0) {
    return 1;
  }
  if (strncmp(name, local_label_prefix, prefix_len) == 0) {
    return 1;
  }
  return 0;
}

/* Parse floating point number from string and compute mantissa and
   exponent. Mantissa is normalized.
*/
#define EXP_MIN -0x10000
#define EXP_MAX 0x10000
static int
str_to_broken_float (bool *signP, uint64_t *mantissaP, int *expP)
{
  const char *current_p = input_line_pointer;
  bool sign;
  uint64_t mantissa = 0;
  int exponent = 0;
  int binary_exponent_i;

  current_p = skip_space(current_p);

  sign = (*current_p == '-');
  *signP = sign;
  if (sign || *current_p == '+')
    current_p++;

  if (strncasecmp(current_p, "NaN", 3) == 0)
    {
      *mantissaP = 0;
      *expP = 0;
      input_line_pointer = (char*)current_p + 3;
      return 1;
    }
  if (strncasecmp(current_p, "inf", 3) == 0)
    {
      *mantissaP = 1ULL << 63;
      *expP = EXP_MAX;
      input_line_pointer = (char*)current_p + 3;
      return 1;
    }

  bool mantissa_reached_precision_limit = false;

  while (ISDIGIT(*current_p))
    {
      if (mantissa >= (1ULL << 60))
        {
          mantissa_reached_precision_limit = true;
          if (*current_p >= '5')
            {
              if (mantissa < UINT64_MAX) mantissa++;
            }
          break;
        }
      
      mantissa = mantissa * 10 + (*current_p - '0');
      current_p++;
    }

  while (ISDIGIT(*current_p))
    {
      exponent++;
      current_p++;
    }

  if (*current_p == '.')
    {
      current_p++;
      if (!exponent)
        {
          while (ISDIGIT(*current_p))
            {
              if (mantissa >= (1ULL << 60))
                {
                  mantissa_reached_precision_limit = true;
                  if (*current_p >= '5')
                    {
                      if (mantissa < UINT64_MAX) mantissa++;
                    }
                  break;
                }

              mantissa = mantissa * 10 + (*current_p - '0');
              exponent--;
              current_p++;
            }
        }
      while (ISDIGIT(*current_p))
        {
          current_p++;
        }
    }

  if (*current_p == 'e' || *current_p == 'E')
    {
      int exp_sign;
      int exp_val = 0;
      current_p++;
      exp_sign = (*current_p == '-');
      if (exp_sign || *current_p == '+')
        current_p++;
      while (ISDIGIT(*current_p))
        {
          if (exp_val < 100)
            exp_val = exp_val * 10 + (*current_p - '0');
          current_p++;
        }
      exponent += (exp_sign) ? -exp_val : exp_val;
    }

  if (ISALNUM(*current_p) || *current_p == '.')
    return 0;

  input_line_pointer = (char*)current_p;

  if (mantissa == 0)
    {
      *mantissaP = 1ULL << 63;
      *expP = EXP_MIN;
      return 1;
    }

  for (; mantissa <= UINT64_MAX / 10; --exponent)
    mantissa *= 10;

  binary_exponent_i = 64;

  while (exponent > 0)
    {
      while (mantissa > UINT64_MAX / 10)
        {
          mantissa >>= 1;
          binary_exponent_i++;
        }
	    mantissa *= 10;
      exponent--;
    }

  while (exponent < 0)
    {
      while (!(mantissa >> 63))
        {
          mantissa <<= 1;
          binary_exponent_i--;
        }
	    mantissa /= 10;
      exponent++;
    }

  while (!(mantissa >> 63) && mantissa != 0)
    {
      mantissa <<= 1;
      binary_exponent_i--;
    }

  *mantissaP = mantissa;
  *expP = binary_exponent_i;
  return 1;
}

static const char *
str_to_zeda32(char *litP, int *sizeP)
{
  const int ZEDA32_OUTPUT_SIZE = 4;
  const int ZEDA32_MANTISSA_BITS = 24;
  const int ZEDA32_BITS_PER_BYTE = 8;

  const int INITIAL_MANTISSA_RIGHT_SHIFT = 39;
  const int MANTISSA_ROUNDING_SHIFT = 1;
  const int MANTISSA_OVERFLOW_BIT_POS = 24;
  const int MANTISSA_IMPLICIT_ONE_MASK_SHIFT = 23;

  const int EXPONENT_ADJUSTMENT_INITIAL = 1;
  const int EXPONENT_MIN_NORMAL = -127;
  const int EXPONENT_MAX_NORMAL = 127;
  const int EXPONENT_SPECIAL_VALUE = -128;
  const int EXPONENT_BYTE_BIAS = 128;

  const uint64_t ZEDA32_MANTISSA_OVERFLOW_POSITIVE = 0x400000ULL;
  const uint64_t ZEDA32_MANTISSA_OVERFLOW_NEGATIVE = 0xc00000ULL;
  const uint64_t ZEDA32_MANTISSA_ZERO_SPECIAL = 0x200000ULL;

  uint64_t mantissa;
  bool sign;
  int exponent;
  unsigned int i;

  *sizeP = ZEDA32_OUTPUT_SIZE;

  if (!str_to_broken_float(&sign, &mantissa, &exponent))
    return _("invalid syntax");

  exponent -= EXPONENT_ADJUSTMENT_INITIAL;

  mantissa >>= INITIAL_MANTISSA_RIGHT_SHIFT;

  ++mantissa;

  mantissa >>= MANTISSA_ROUNDING_SHIFT;

  if (mantissa >> MANTISSA_OVERFLOW_BIT_POS)
    {
      mantissa >>= MANTISSA_ROUNDING_SHIFT;
      ++exponent;
    }

  if (exponent < EXPONENT_MIN_NORMAL)
    {
      exponent = EXPONENT_SPECIAL_VALUE;
      mantissa = 0;
    }
  else if (exponent > EXPONENT_MAX_NORMAL)
    {
      exponent = EXPONENT_SPECIAL_VALUE;
      mantissa = sign ? ZEDA32_MANTISSA_OVERFLOW_NEGATIVE : ZEDA32_MANTISSA_OVERFLOW_POSITIVE;
    }
  else if (mantissa == 0)
    {
      exponent = EXPONENT_SPECIAL_VALUE;
      mantissa = ZEDA32_MANTISSA_ZERO_SPECIAL;
    }
  else if (!sign)
    mantissa &= (1ULL << MANTISSA_IMPLICIT_ONE_MASK_SHIFT) - 1;

  for (i = 0; i < ZEDA32_MANTISSA_BITS; i += ZEDA32_BITS_PER_BYTE)
    *litP++ = (char)(mantissa >> i);

  *litP = (char)(exponent + EXPONENT_BYTE_BIAS);

  return NULL;
}

/*
  Math48 by Anders Hejlsberg support.
  Mantissa is 39 bits wide, exponent 8 bit wide.
  Format is:
  bit 47: sign
  bit 46-8: normalized mantissa (bits 38-0, bit39 assumed to be 1)
  bit 7-0: exponent+128 (0 - value is null)
  MIN: 2.938735877e-39
  MAX: 1.701411835e+38
*/
#define FLOAT48_BYTE_COUNT 6
#define MANTISSA_FINAL_BITS 40
#define EXPONENT_BITS 8

#define INITIAL_MANTISSA_PREP_SHIFT 23
#define EXPONENT_BIAS 128
#define EXPONENT_MIN_VALUE -127
#define EXPONENT_MAX_VALUE 127
#define MANTISSA_OVERFLOW_DETECTION_BIT (1ULL << MANTISSA_FINAL_BITS)
#define MANTISSA_POSITIVE_MSB_CLEAR_MASK ((1ULL << (MANTISSA_FINAL_BITS - 1)) - 1)

static const char *
str_to_float48(char *litP, int *sizeP)
{
  uint64_t mantissa_val;
  bool is_negative_sign;
  int exponent_val;
  unsigned int byte_idx;

  *sizeP = FLOAT48_BYTE_COUNT;

  if (!str_to_broken_float (&is_negative_sign, &mantissa_val, &exponent_val))
    {
      return _("invalid syntax");
    }

  mantissa_val >>= INITIAL_MANTISSA_PREP_SHIFT;

  mantissa_val++;
  mantissa_val >>= 1;

  if (mantissa_val & MANTISSA_OVERFLOW_DETECTION_BIT)
    {
      mantissa_val >>= 1;
      exponent_val++;
    }

  if (exponent_val < EXPONENT_MIN_VALUE)
    {
      memset (litP, 0, FLOAT48_BYTE_COUNT);
      return NULL;
    }

  if (exponent_val > EXPONENT_MAX_VALUE)
    {
      return _("overflow");
    }

  if (!is_negative_sign)
    {
      mantissa_val &= MANTISSA_POSITIVE_MSB_CLEAR_MASK;
    }

  *litP++ = (char)(exponent_val + EXPONENT_BIAS);

  for (byte_idx = 0; byte_idx < MANTISSA_FINAL_BITS; byte_idx += 8)
    {
      *litP++ = (char)(mantissa_val >> byte_idx);
    }

  return NULL;
}

static const char *
str_to_ieee754_half(const char *litP, int *sizeP)
{
  return ieee_md_atof ('h', litP, sizeP, false);
}

static const char *
str_to_ieee754_s(char *litP, int *sizeP)
{
  if (litP == NULL || sizeP == NULL) {
    return NULL;
  }
  return ieee_md_atof ('s', litP, sizeP, false);
}

static const char *
str_to_ieee754_d(const char *litP, int *sizeP)
{
  /*
   * Improve reliability and security by validating input parameters.
   * Passing NULL for an output parameter like 'sizeP' would lead to a crash
   * if ieee_md_atof attempts to dereference it.
   * Passing NULL for 'litP' means there's no string to parse.
   * For legacy code, it's safer to assume the callee might not handle NULL inputs gracefully.
   */
  if (sizeP == NULL) {
    /* Cannot fulfill contract as output parameter for size is missing. */
    return NULL;
  }

  if (litP == NULL) {
    /* No input string to parse. Indicate no characters processed. */
    *sizeP = 0;
    return NULL;
  }

  /*
   * Change 'char *litP' to 'const char *litP'.
   * This indicates that the function (and by extension, ieee_md_atof)
   * will not modify the input string, improving maintainability and type safety.
   * This is a non-breaking change if ieee_md_atof truly only reads 'litP'.
   */
  return ieee_md_atof('d', litP, sizeP, false);
}

#ifdef TARGET_USE_CFIPOP
/* Initialize the DWARF-2 unwind information for this procedure. */
void
z80_tc_frame_initial_instructions (void)
{
  static int sp_regno = -1;

  if (sp_regno < 0)
    sp_regno = z80_tc_regname_to_dw2regnum ("sp");

  cfi_add_CFA_def_cfa (sp_regno, 0);
}

int
z80_tc_regname_to_dw2regnum (const char *regname)
{
  static const char *regs[] =
    { /* same registers as for GDB */
      "af", "bc", "de", "hl",
      "sp", "pc", "ix", "iy",
      "af_", "bc_", "de_", "hl_",
      "ir"
    };
  unsigned i;

  for (i = 0; i < ARRAY_SIZE(regs); ++i)
    if (!strcasecmp (regs[i], regname))
      return i;

  return -1;
}
#endif

/* Implement DWARF2_ADDR_SIZE.  */
int
z80_dwarf2_addr_size (const bfd *abfd)
{
  static const int DEFAULT_ADDR_SIZE = 2;
  static const int EZ80_ADL_SPECIFIC_ADDR_SIZE = 3;

  if (abfd == NULL)
    {
      return -1;
    }

  switch (bfd_get_mach (abfd))
    {
    case bfd_mach_ez80_adl:
      return EZ80_ADL_SPECIFIC_ADDR_SIZE;
    default:
      return DEFAULT_ADDR_SIZE;
    }
}
