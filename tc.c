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

static int find_cpu_match(const char *name, size_t len, int *ok, int *err, int *mode)
{
  unsigned i;
  for (i = 0; i < ARRAY_SIZE(match_cpu_table); ++i)
    if (!strncasecmp(name, match_cpu_table[i].name, len) &&
        strlen(match_cpu_table[i].name) == len)
      {
        *ok = match_cpu_table[i].ins_ok;
        *err = match_cpu_table[i].ins_err;
        *mode = match_cpu_table[i].cpu_mode;
        return 1;
      }
  return 0;
}

static int find_extension_match(const char *name, size_t len)
{
  unsigned i;
  for (i = 0; i < ARRAY_SIZE(match_ext_table); ++i)
    if (!strncasecmp(name, match_ext_table[i].name, len) &&
        strlen(match_ext_table[i].name) == len)
      return i;
  return -1;
}

static void apply_extension(char operation, int ext_index, int *ok, int *err, int *mode)
{
  if (operation == '+')
    {
      *ok |= match_ext_table[ext_index].ins_ok;
      *err &= ~match_ext_table[ext_index].ins_ok;
      *mode |= match_ext_table[ext_index].cpu_mode;
    }
  else
    {
      *ok &= ~match_ext_table[ext_index].ins_ok;
      *err |= match_ext_table[ext_index].ins_ok;
      *mode &= ~match_ext_table[ext_index].cpu_mode;
    }
}

static void process_extensions(const char *name, int *ok, int *err, int *mode)
{
  size_t len = strcspn(name, "+-");
  
  while (name[len])
    {
      char operation = name[len];
      name = &name[len + 1];
      len = strcspn(name, "+-");
      
      int ext_index = find_extension_match(name, len);
      if (ext_index < 0)
        as_fatal(_("Invalid EXTENSION is specified: %s"), name);
      
      apply_extension(operation, ext_index, ok, err, mode);
    }
}

static void setup_march(const char *name, int *ok, int *err, int *mode)
{
  size_t len = strcspn(name, "+-");
  
  if (!find_cpu_match(name, len, ok, err, mode))
    as_fatal(_("Invalid CPU is specified: %s"), name);
  
  process_extensions(name, ok, err, mode);
}

static int get_instruction_flag(const char *inst)
{
  if (!strcmp(inst, "idx-reg-halves"))
    return INS_IDX_HALF;
  if (!strcmp(inst, "sli"))
    return INS_SLI;
  if (!strcmp(inst, "op-ii-ld"))
    return INS_ROT_II_LD;
  if (!strcmp(inst, "in-f-c"))
    return INS_IN_F_C;
  if (!strcmp(inst, "out-c-0"))
    return INS_OUT_C_0;
  return 0;
}

static void apply_instruction_flag(int flag, int *add, int *sub)
{
  *add |= flag;
  *sub &= ~flag;
}

static int setup_instruction(const char *inst, int *add, int *sub)
{
  int flag = get_instruction_flag(inst);
  if (flag == 0)
    return 0;
  apply_instruction_flag(flag, add, sub);
  return 1;
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
  if (strcasecmp (arg, "zeda32") == 0)
    return str_to_zeda32;

  if (strcasecmp (arg, "math48") == 0)
    return str_to_float48;

  if (strcasecmp (arg, "half") == 0)
    return str_to_ieee754_h;

  if (strcasecmp (arg, "single") == 0)
    return str_to_ieee754_s;

  if (strcasecmp (arg, "double") == 0)
    return str_to_ieee754_d;

  if (strcasecmp (arg, "ieee754") == 0)
    as_fatal (_("invalid floating point numbers type `%s'"), arg);
  
  return NULL;
}

static int extract_instruction_token(const char *start, char *buffer, int buffer_size)
{
    const char *end = strchr(start, ',');
    int length = (end == NULL) ? strlen(start) : (end - start);
    
    if (length == 0 || length >= buffer_size)
        return -1;
    
    memcpy(buffer, start, length);
    buffer[length] = '\0';
    return length;
}

static int process_single_instruction(const char *instruction, int *add, int *sub)
{
    if (!setup_instruction(instruction, add, sub))
    {
        as_bad(_("invalid INST in command line: %s"), instruction);
        return 0;
    }
    return 1;
}

static const char* advance_to_next_instruction(const char *current, int token_length)
{
    current += token_length;
    if (*current == ',')
        current++;
    return current;
}

static int setup_instruction_list(const char *list, int *add, int *sub)
{
    #define INSTRUCTION_BUFFER_SIZE 16
    char buffer[INSTRUCTION_BUFFER_SIZE];
    int count = 0;
    
    for (const char *current = list; *current != '\0';)
    {
        int token_length = extract_instruction_token(current, buffer, INSTRUCTION_BUFFER_SIZE);
        
        if (token_length < 0)
        {
            as_bad(_("invalid INST in command line: %s"), current);
            return 0;
        }
        
        if (!process_single_instruction(buffer, add, sub))
            return 0;
        
        count++;
        current = advance_to_next_instruction(current, token_length);
    }
    
    return count;
}

int
md_parse_option (int c, const char* arg)
{
  switch (c)
    {
    default:
      return 0;
    case OPTION_MARCH:
      setup_march (arg, & ins_ok, & ins_err, & cpu_mode);
      break;
    case OPTION_MACH_Z80:
      setup_march ("z80", & ins_ok, & ins_err, & cpu_mode);
      break;
    case OPTION_MACH_R800:
      setup_march ("r800", & ins_ok, & ins_err, & cpu_mode);
      break;
    case OPTION_MACH_Z180:
      setup_march ("z180", & ins_ok, & ins_err, & cpu_mode);
      break;
    case OPTION_MACH_EZ80_Z80:
      setup_march ("ez80", & ins_ok, & ins_err, & cpu_mode);
      break;
    case OPTION_MACH_EZ80_ADL:
      setup_march ("ez80+adl", & ins_ok, & ins_err, & cpu_mode);
      break;
    case OPTION_FP_SINGLE_FORMAT:
      str_to_float = get_str_to_float (arg);
      break;
    case OPTION_FP_DOUBLE_FORMAT:
      str_to_double = get_str_to_float (arg);
      break;
    case OPTION_MACH_INST:
      if ((ins_ok & INS_GBZ80) == 0)
        return setup_instruction_list (arg, & ins_ok, & ins_err);
      break;
    case OPTION_MACH_NO_INST:
      if ((ins_ok & INS_GBZ80) == 0)
        return setup_instruction_list (arg, & ins_err, & ins_ok);
      break;
    case OPTION_MACH_WUD:
    case OPTION_MACH_IUD:
      if ((ins_ok & INS_GBZ80) == 0)
        {
          ins_ok |= INS_UNDOC;
          ins_err &= ~INS_UNDOC;
        }
      break;
    case OPTION_MACH_WUP:
    case OPTION_MACH_IUP:
      if ((ins_ok & INS_GBZ80) == 0)
        {
          ins_ok |= INS_UNDOC | INS_UNPORT;
          ins_err &= ~(INS_UNDOC | INS_UNPORT);
        }
      break;
    case OPTION_MACH_FUD:
      if ((ins_ok & (INS_R800 | INS_GBZ80)) == 0)
	{
	  ins_ok &= (INS_UNDOC | INS_UNPORT);
	  ins_err |= INS_UNDOC | INS_UNPORT;
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

void
md_show_usage (FILE * f)
{
  fprintf (f, _("\nCPU model options:\n"
                "  -march=CPU[+EXT...][-EXT...]\n"
                "\t\t\t  generate code for CPU, where CPU is one of:\n"));
  
  print_table(f, match_cpu_table, ARRAY_SIZE(match_cpu_table));
  
  fprintf (f, _("And EXT is combination (+EXT - add, -EXT - remove) of:\n"));
  
  print_table(f, match_ext_table, ARRAY_SIZE(match_ext_table));
  
  print_compatibility_options(f);
}

static void
print_table(FILE *f, const struct {const char *name; const char *comment;} *table, size_t size)
{
  unsigned i;
  for (i = 0; i < size; ++i)
    fprintf (f, "  %-8s\t\t  %s\n", table[i].name, table[i].comment);
}

static void
print_compatibility_options(FILE *f)
{
  fprintf (f, _("\n"
                "Compatibility options:\n"
                "  -local-prefix=TEXT\t  treat labels prefixed by TEXT as local\n"
                "  -colonless\t\t  permit colonless labels\n"
                "  -sdcc\t\t\t  accept SDCC specific instruction syntax\n"
                "  -fp-s=FORMAT\t\t  set single precision FP numbers format\n"
                "  -fp-d=FORMAT\t\t  set double precision FP numbers format\n"
                "Where FORMAT one of:\n"
                "  ieee754\t\t  IEEE754 compatible (depends on directive)\n"
                "  half\t\t\t  IEEE754 half precision (16 bit)\n"
                "  single\t\t  IEEE754 single precision (32 bit)\n"
                "  double\t\t  IEEE754 double precision (64 bit)\n"
                "  zeda32\t\t  Zeda z80float library 32 bit format\n"
                "  math48\t\t  48 bit format from Math48 library\n"
                "\n"
                "Default: -march=z80+xyhl+infc\n"));
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
  expressionS nul, reg;
  char * p;
  char buf[BUFLEN];

  memset (&reg, 0, sizeof (reg));
  memset (&nul, 0, sizeof (nul));

  if (ins_ok & INS_EZ80)
    listing_lhs_width = 6;

  initialize_register_expression(&reg);
  register_all_symbols(&reg, buf);
  
  p = input_line_pointer;
  input_line_pointer = (char *) "0";
  nul.X_md = 0;
  expression (&nul);
  input_line_pointer = p;
  zero = make_expr_symbol (&nul);
  linkrelax = 0;
}

static void initialize_register_expression(expressionS *reg)
{
  reg->X_op = O_register;
  reg->X_md = 0;
  reg->X_add_symbol = reg->X_op_symbol = 0;
}

static void register_all_symbols(expressionS *reg, char *buf)
{
  unsigned int i;
  
  for (i = 0; i < ARRAY_SIZE(regtable); ++i)
    {
      if (should_skip_register(i))
        continue;
      
      reg->X_add_number = regtable[i].number;
      register_symbol_variations(reg, regtable[i].name, buf);
    }
}

static int should_skip_register(unsigned int index)
{
  return regtable[index].isa && !(regtable[index].isa & ins_ok);
}

static void register_symbol_variations(expressionS *reg, const char *name, char *buf)
{
  unsigned int name_length = strlen(name);
  unsigned int variation_count;
  unsigned int j;
  
  if (name_length + 1 >= BUFLEN)
    return;
  
  buf[name_length] = 0;
  variation_count = 1 << name_length;
  
  for (j = variation_count; j > 0; --j)
    {
      create_symbol_variation(name, buf, j, name_length);
      register_single_symbol(buf, reg);
    }
}

static void create_symbol_variation(const char *name, char *buf, unsigned int variation_mask, unsigned int length)
{
  unsigned int k;
  
  for (k = 0; k < length; ++k)
    {
      buf[k] = (variation_mask & (1 << k)) ? TOUPPER(name[k]) : name[k];
    }
}

static void register_single_symbol(const char *name, expressionS *reg)
{
  symbolS *psym = symbol_find_or_make(name);
  S_SET_SEGMENT(psym, reg_section);
  symbol_set_value_expression(psym, reg);
}

void
z80_md_finish (void)
{
  int mach_type = get_machine_type(ins_ok & INS_MARCH_MASK);
  bfd_set_arch_mach (stdoutput, TARGET_ARCH, mach_type);
}

static int
get_machine_type (int instruction_mask)
{
  switch (instruction_mask)
    {
    case INS_Z80:
      return bfd_mach_z80;
    case INS_R800:
      return bfd_mach_r800;
    case INS_Z180:
      return bfd_mach_z180;
    case INS_GBZ80:
      return bfd_mach_gbz80;
    case INS_EZ80:
      return cpu_mode ? bfd_mach_ez80_adl : bfd_mach_ez80_z80;
    case INS_Z80N:
      return bfd_mach_z80n;
    default:
      return 0;
    }
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

static const char *skip_space(const char *s)
{
    while (is_whitespace(*s))
        ++s;
    return s;
}

/* A non-zero return-value causes a continue in the
   function read_a_source_file () in ../read.c.  */
int
z80_start_line_hook (void)
{
  char *p;

  for (p = input_line_pointer; *p && *p != '\n'; ++p)
    {
      if (*p == '\'')
        {
          if (!handle_single_quote(&p))
            continue;
        }
      else if (*p == '"')
        {
          if (!handle_double_quote(&p))
            return 1;
        }
      else if (*p == '#')
        {
          handle_hash_symbol(&p);
        }
    }

  if (sdcc_compat && *input_line_pointer == '0')
    process_dollar_labels();

  if (is_name_beginner(*input_line_pointer))
    return process_label_assignment();

  return 0;
}

static int
handle_single_quote (char **p)
{
  char buf[4];
  
  if ((*p)[1] != 0 && (*p)[1] != '\'' && (*p)[2] == '\'')
    {
      snprintf(buf, 4, "%3d", (unsigned char)(*p)[1]);
      *(*p)++ = buf[0];
      *(*p)++ = buf[1];
      *(*p)++ = buf[2];
      return 1;
    }
  return 0;
}

static int
handle_double_quote (char **p)
{
  char quote = **p;
  
  (*p)++;
  while (quote != **p && '\n' != **p)
    (*p)++;
    
  if (quote != **p)
    {
      as_bad(_("-- unterminated string"));
      ignore_rest_of_line();
      return 0;
    }
  return 1;
}

static void
handle_hash_symbol (char **p)
{
  if (!sdcc_compat)
    return;
    
  if (is_whitespace((*p)[1]) && *skip_space(*p + 1) == '(')
    {
      *(*p)++ = '0';
      **p = '+';
    }
  else
    **p = ((*p)[1] == '(') ? '+' : ' ';
}

static void
process_dollar_labels (void)
{
  char *p;
  char *dollar;

  for (p = input_line_pointer; *p >= '0' && *p <= '9'; ++p)
    ;

  if (p[0] == '$' && p[1] == ':')
    {
      dollar = p;
      for (p = input_line_pointer; *p == '0' && p < dollar - 1; ++p)
        *p = ' ';
    }
}

static int
process_label_assignment (void)
{
  char *name;
  char c, *rest, *line_start;
  int len;

  line_start = input_line_pointer;
  if (ignore_input())
    return 0;

  c = get_symbol_name(&name);
  rest = input_line_pointer + 1;

  if (c == ':' && *rest == ':')
    {
      if (sdcc_compat)
        *rest = ' ';
      ++rest;
    }

  rest = (char*)skip_space(rest);
  len = get_assignment_type(rest);

  if (len && is_valid_assignment(rest, len))
    {
      handle_assignment(line_start, rest, len, name);
      return 1;
    }

  restore_line_pointer(c);
  input_line_pointer = line_start;
  return 0;
}

static int
get_assignment_type (char *rest)
{
  if (*rest == '=')
    return (rest[1] == '=') ? 2 : 1;

  if (*rest == '.')
    ++rest;

  if (strncasecmp(rest, "EQU", 3) == 0)
    return 3;
  
  if (strncasecmp(rest, "DEFL", 4) == 0)
    return 4;

  return 0;
}

static int
is_valid_assignment (char *rest, int len)
{
  return len <= 2 || !ISALPHA(rest[len]);
}

static void
handle_assignment (char *line_start, char *rest, int len, char *name)
{
  if (line_start[-1] == '\n')
    {
      bump_line_counters();
      LISTING_NEWLINE();
    }

  input_line_pointer = rest + len - 1;

  switch (len)
    {
    case 1:
    case 4:
      equals(name, 1);
      break;
    case 2:
    case 3:
      equals(name, 0);
      break;
    }
}

symbolS *
md_undefined_symbol (char *name ATTRIBUTE_UNUSED)
{
  return NULL;
}

const char *md_atof(int type, char *litP, int *sizeP)
{
    if (is_float_type(type) && str_to_float)
        return str_to_float(litP, sizeP);
    
    if (is_double_type(type) && str_to_double)
        return str_to_double(litP, sizeP);
    
    return ieee_md_atof(type, litP, sizeP, false);
}

static int is_float_type(int type)
{
    return type == 'f' || type == 'F' || type == 's' || type == 'S';
}

static int is_double_type(int type)
{
    return type == 'd' || type == 'D' || type == 'r' || type == 'R';
}

valueT
md_section_align (segT seg ATTRIBUTE_UNUSED, valueT size)
{
  return size;
}

long
md_pcrel_from (fixS * fixp)
{
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
static int key_cmp(const void *a, const void *b)
{
    const char *str_a = *(const char **)a;
    const char *str_b = *(const char **)b;
    return strcmp(str_a, str_b);
}

char buf[BUFLEN];
const char *key = buf;

/* Prevent an error on a line from also generating
   a "junk at end of line" error message.  */
static char err_flag;

static void
error (const char * message)
{
  if (err_flag)
    return;

  as_bad ("%s", message);
  err_flag = 1;
}

static void ill_op(void)
{
    error(_("illegal operand"));
}

static void
wrong_mach (int ins_type)
{
  if (ins_type & ins_err)
    ill_op ();
  else
    as_warn (_("undocumented instruction"));
}

static void
check_mach (int ins_type)
{
  if ((ins_type & ins_ok) == 0)
    wrong_mach (ins_type);
}

/* Check whether an expression is indirect.  */
static int skip_quoted_string(const char **p)
{
    char quote = **p;
    (*p)++;
    while (**p != quote && **p != '\n')
    {
        if (**p == '\\' && (*p)[1])
            (*p)++;
        (*p)++;
    }
    return 0;
}

static int handle_opening_paren(int *depth)
{
    (*depth)++;
    return 0;
}

static int handle_closing_paren(const char **p, int *depth, int *indir)
{
    (*depth)--;
    if (*depth == 0)
    {
        const char *next = skip_space(*p + 1);
        if (*next && *next != ',')
            *indir = 0;
        *p = next - 1;
    }
    if (*depth < 0)
        error(_("mismatched parentheses"));
    return 0;
}

static int process_character(const char **p, int *depth, int *indir)
{
    switch (**p)
    {
    case '"':
    case '\'':
        return skip_quoted_string(p);
    case '(':
        return handle_opening_paren(depth);
    case ')':
        return handle_closing_paren(p, depth, indir);
    default:
        return 0;
    }
}

static int is_indir(const char *s)
{
    const char *p = s;
    int depth = 0;
    int indir = (*s == '(');

    while (*p && *p != ',')
    {
        process_character(&p, &depth, &indir);
        p++;
    }

    if (depth != 0)
        error(_("mismatched parentheses"));

    return indir;
}

/* Check whether a symbol involves a register.  */
static bool check_symbol_for_register(expressionS *ex, symbolS *symbol)
{
    return symbol && contains_register(symbol);
}

static bool check_binary_operation(expressionS *ex)
{
    return check_symbol_for_register(ex, ex->X_op_symbol) ||
           check_symbol_for_register(ex, ex->X_add_symbol);
}

static bool check_unary_operation(expressionS *ex)
{
    return check_symbol_for_register(ex, ex->X_add_symbol);
}

static bool
contains_register (symbolS *sym)
{
    if (!sym)
        return false;

    expressionS *ex = symbol_get_value_expression(sym);

    switch (ex->X_op)
    {
    case O_register:
        return true;

    case O_add:
    case O_subtract:
        return check_binary_operation(ex);

    case O_uminus:
    case O_symbol:
        return check_unary_operation(ex);

    default:
        return false;
    }
}

/* Parse general expression, not looking for indexed addressing.  */
static const char *handle_sdcc_shift_prefix(const char **p, int *make_shift)
{
    if (!sdcc_compat || (**p != '<' && **p != '>'))
        return *p;
    
    *make_shift = (**p == '<') ? 0 : (cpu_mode ? 16 : 8);
    (*p)++;
    *p = skip_space(*p);
    return *p;
}

static int check_gbz80_indirect_hl(const char *p, expressionS *op)
{
    if (!(ins_ok & INS_GBZ80))
        return 0;
    
    p = skip_space(p + 1);
    if (strncasecmp(p, "hl", 2) != 0)
        return 0;
    
    p = skip_space(p + 2);
    if (*skip_space(p + 1) != ')' || (*p != '+' && *p != '-'))
        return 0;
    
    op->X_op = O_md1;
    op->X_add_symbol = NULL;
    op->X_add_number = (*p == '+') ? REG_HL : -REG_HL;
    input_line_pointer = (char*)skip_space(p + 1) + 1;
    return 1;
}

static void validate_expression(expressionS *op)
{
    switch (op->X_op)
    {
    case O_absent:
        error(_("missing operand"));
        break;
    case O_illegal:
        error(_("bad expression syntax"));
        break;
    default:
        break;
    }
}

static void apply_shift_operation(expressionS *op, int make_shift)
{
    expressionS data;
    
    if (make_shift < 0)
        return;
    
    op->X_add_symbol = make_expr_symbol(op);
    op->X_add_number = 0;
    op->X_op = O_right_shift;
    
    memset(&data, 0, sizeof(data));
    data.X_op = O_constant;
    data.X_add_number = make_shift;
    op->X_op_symbol = make_expr_symbol(&data);
}

static const char *parse_exp_not_indexed(const char *s, expressionS *op)
{
    const char *p;
    int indir;
    int make_shift = -1;
    
    memset(op, 0, sizeof(*op));
    p = skip_space(s);
    
    s = handle_sdcc_shift_prefix(&p, &make_shift);
    
    indir = (make_shift == -1) ? is_indir(p) : 0;
    op->X_md = indir;
    
    if (indir && check_gbz80_indirect_hl(p, op))
        return input_line_pointer;
    
    input_line_pointer = (char*)s;
    expression(op);
    resolve_register(op);
    validate_expression(op);
    apply_shift_operation(op, make_shift);
    
    return input_line_pointer;
}

static int is_valid_index_register(int rnum)
{
    return (rnum == REG_IX || rnum == REG_IY);
}

static int validate_index_operation(expressionS *op)
{
    if (O_register != symbol_get_value_expression(op->X_add_symbol)->X_op)
        return 0;

    int rnum = symbol_get_value_expression(op->X_add_symbol)->X_add_number;
    if (!is_valid_index_register(rnum) || contains_register(op->X_op_symbol))
    {
        ill_op();
        return 0;
    }
    
    return rnum;
}

static void convert_subtraction_to_addition(expressionS *op)
{
    expressionS minus;
    memset(&minus, 0, sizeof(minus));
    minus.X_op = O_uminus;
    minus.X_add_symbol = op->X_op_symbol;
    op->X_op_symbol = make_expr_symbol(&minus);
    op->X_op = O_add;
}

static void clear_add_number(expressionS *op)
{
    if (op->X_add_number != 0)
    {
        expressionS add;
        memset(&add, 0, sizeof(add));
        add.X_op = O_symbol;
        add.X_add_number = op->X_add_number;
        add.X_add_symbol = op->X_op_symbol;
        op->X_add_symbol = make_expr_symbol(&add);
    }
    else
    {
        op->X_add_symbol = op->X_op_symbol;
    }
}

static int unify_indexed(expressionS *op)
{
    int rnum = validate_index_operation(op);
    if (rnum == 0)
        return 0;

    if (O_subtract == op->X_op)
        convert_subtraction_to_addition(op);

    clear_add_number(op);

    op->X_add_number = rnum;
    op->X_op_symbol = 0;
    return 1;
}

/* Parse expression, change operator to O_md1 for indexed addressing.  */
static const char *
parse_exp (const char *s, expressionS *op)
{
  const char* res = parse_exp_not_indexed (s, op);
  
  switch (op->X_op)
    {
    case O_add:
    case O_subtract:
      if (unify_indexed (op) && op->X_md)
        op->X_op = O_md1;
      break;
      
    case O_register:
      if (op->X_md && ((REG_IX == op->X_add_number) || (REG_IY == op->X_add_number)))
        {
          op->X_add_symbol = zero;
          op->X_op = O_md1;
        }
      break;
      
    case O_constant:
      if (sdcc_compat && is_indir (res))
        res = handle_sdcc_index_syntax (res, op);
      break;
      
    default:
      break;
    }
    
  return res;
}

static const char *
handle_sdcc_index_syntax (const char *res, expressionS *op)
{
  expressionS off = *op;
  res = parse_exp (res, op);
  
  if (op->X_op != O_md1 || op->X_add_symbol != zero)
    {
      ill_op ();
    }
  else
    {
      op->X_add_symbol = make_expr_symbol (&off);
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
  int length = extract_alpha_to_buffer(s);
  
  if (!is_valid_terminator(s, length))
    return NULL;
    
  struct reg_entry *cc_p = find_condition_code();
  
  if (!cc_p)
    return NULL;
    
  *op = cc_p->number;
  return s + length;
}

static int extract_alpha_to_buffer(const char *s)
{
  int i;
  for (i = 0; i < BUFLEN; ++i)
    {
      if (!ISALPHA(s[i]))
        break;
      buf[i] = TOLOWER(s[i]);
    }
  return i;
}

static int is_valid_terminator(const char *s, int position)
{
  if (position >= BUFLEN)
    return 0;
    
  buf[position] = 0;
  return (s[position] == 0) || (s[position] == ',');
}

static struct reg_entry* find_condition_code(void)
{
  return bsearch(&key, cc_tab, ARRAY_SIZE(cc_tab),
                 sizeof(cc_tab[0]), key_cmp);
}

static const char *
emit_insn (char prefix, char opcode, const char * args)
{
  int size = prefix ? 2 : 1;
  char *p = frag_more(size);
  
  if (prefix)
    {
      *p++ = prefix;
    }
  *p = opcode;
  return args;
}

void z80_cons_fix_new (fragS *frag_p, int offset, int nbytes, expressionS *exp)
{
  #define MIN_RELOC_BYTES 1
  #define MAX_RELOC_BYTES 4
  
  bfd_reloc_code_real_type r[4] =
    {
      BFD_RELOC_8,
      BFD_RELOC_16,
      BFD_RELOC_24,
      BFD_RELOC_32
    };

  if (nbytes < MIN_RELOC_BYTES || nbytes > MAX_RELOC_BYTES)
    {
      as_bad (_("unsupported BFD relocation size %u"), nbytes);
    }
  else
    {
      fix_new_exp (frag_p, offset, nbytes, exp, 0, r[nbytes-1]);
    }
}

static void emit_constant_value(char *p, int64_t value, int size)
{
    for (int i = 0; i < size; ++i)
        p[i] = (value >> (i * 8)) & 0xff;
}

static void check_overflow(expressionS *val, int size)
{
    const int bits = size * 8;
    if (!val->X_extrabit && is_overflow(val->X_add_number, bits))
        as_warn(_("%d-bit overflow (%+" PRId64 ")"), bits, (int64_t)val->X_add_number);
}

static bfd_reloc_code_real_type get_base_relocation(int size)
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
        return BFD_RELOC_NONE;
    }
}

static void validate_expression(expressionS *val)
{
    if ((val->X_op == O_register) ||
        (val->X_op == O_md1) ||
        contains_register(val->X_add_symbol) ||
        contains_register(val->X_op_symbol))
    {
        ill_op();
    }
}

static bfd_reloc_code_real_type get_byte_relocation(int shift)
{
    switch (shift)
    {
    case 0: return BFD_RELOC_Z80_BYTE0;
    case 8: return BFD_RELOC_Z80_BYTE1;
    case 16: return BFD_RELOC_Z80_BYTE2;
    case 24: return BFD_RELOC_Z80_BYTE3;
    default: return BFD_RELOC_NONE;
    }
}

static bfd_reloc_code_real_type get_word_relocation(int shift)
{
    switch (shift)
    {
    case 0: return BFD_RELOC_Z80_WORD0;
    case 16: return BFD_RELOC_Z80_WORD1;
    default: return BFD_RELOC_NONE;
    }
}

static void simplify_expression(expressionS *val)
{
    val->X_op = O_symbol;
    val->X_op_symbol = NULL;
    val->X_add_number = 0;
}

static int calculate_shift(expressionS *val, int size)
{
    int shift = symbol_get_value_expression(val->X_op_symbol)->X_add_number;
    const int mask = (1 << (size * 8)) - 1;
    
    if (val->X_op == O_bit_and && shift == mask)
        return 0;
    else if (val->X_op != O_right_shift)
        return -1;
    
    return shift;
}

static void handle_special_word_shift(char **p, expressionS *val, int shift, int *size, bfd_reloc_code_real_type *r_type)
{
    const int SHIFT_8 = 8;
    const int SHIFT_24 = 24;
    
    simplify_expression(val);
    
    if (shift == SHIFT_8)
    {
        fix_new_exp(frag_now, (*p)++ - frag_now->fr_literal, 1, val, false, BFD_RELOC_Z80_BYTE1);
        *r_type = BFD_RELOC_Z80_BYTE2;
    }
    else
    {
        *r_type = BFD_RELOC_Z80_BYTE3;
    }
    
    *size = 1;
}

static bool process_shifted_expression(char **p, expressionS *val, int *size, bfd_reloc_code_real_type *r_type)
{
    if (*size > 2 || !val->X_op_symbol)
        return false;
    
    int shift = calculate_shift(val, *size);
    bool simplify = true;
    
    if (*size == 1)
    {
        *r_type = get_byte_relocation(shift);
        if (*r_type == BFD_RELOC_NONE)
            simplify = false;
    }
    else
    {
        const int SHIFT_8 = 8;
        const int SHIFT_24 = 24;
        
        if (shift == SHIFT_8 || shift == SHIFT_24)
        {
            handle_special_word_shift(p, val, shift, size, r_type);
            return true;
        }
        
        *r_type = get_word_relocation(shift);
        if (*r_type == BFD_RELOC_NONE)
            simplify = false;
    }
    
    if (simplify)
        simplify_expression(val);
    
    return simplify;
}

static void emit_data_val(expressionS *val, int size)
{
    char *p = frag_more(size);
    
    if (val->X_op == O_constant)
    {
        check_overflow(val, size);
        emit_constant_value(p, val->X_add_number, size);
        return;
    }
    
    bfd_reloc_code_real_type r_type = get_base_relocation(size);
    validate_expression(val);
    
    if (!process_shifted_expression(&p, val, &size, &r_type))
    {
        /* Keep original r_type if not processed */
    }
    
    fix_new_exp(frag_now, p - frag_now->fr_literal, size, val, false, r_type);
}

static void emit_byte_reloc_8(expressionS *val)
{
    emit_data_val(val, 1);
}

static void check_register_operands(expressionS *val)
{
    if (contains_register(val->X_add_symbol) || contains_register(val->X_op_symbol))
    {
        ill_op();
    }
}

static void validate_pcrel_constant(expressionS *val, bfd_reloc_code_real_type r_type)
{
    if ((r_type == BFD_RELOC_8_PCREL) && (val->X_op == O_constant))
    {
        as_bad(_("cannot make a relative jump to an absolute location"));
    }
}

static void validate_constant_range(expressionS *val, bfd_reloc_code_real_type r_type)
{
    #define BYTE_MIN -128
    #define BYTE_MAX 127
    
    if (val->X_op != O_constant)
        return;
        
    if ((val->X_add_number < BYTE_MIN) || (val->X_add_number > BYTE_MAX))
    {
        const char *error_msg = (r_type == BFD_RELOC_Z80_DISP8) 
            ? _("index overflow (%+" PRId64 ")")
            : _("offset overflow (%+" PRId64 ")");
        as_bad(error_msg, (int64_t)val->X_add_number);
    }
}

static void create_fixup(char *p, expressionS *val, bfd_reloc_code_real_type r_type)
{
    if (val->X_op != O_constant)
    {
        fix_new_exp(frag_now, p - frag_now->fr_literal, 1, val,
                   r_type == BFD_RELOC_8_PCREL, r_type);
    }
}

static void emit_byte(expressionS *val, bfd_reloc_code_real_type r_type)
{
    if (r_type == BFD_RELOC_8)
    {
        emit_byte_reloc_8(val);
        return;
    }
    
    char *p = frag_more(1);
    *p = val->X_add_number;
    
    check_register_operands(val);
    validate_pcrel_constant(val, r_type);
    validate_constant_range(val, r_type);
    create_fixup(p, val, r_type);
}

static void
emit_word (expressionS * val)
{
  #define WORD_SIZE_IL_MODE 3
  #define WORD_SIZE_NORMAL_MODE 2
  
  int word_size = (inst_mode & INST_MODE_IL) ? WORD_SIZE_IL_MODE : WORD_SIZE_NORMAL_MODE;
  emit_data_val (val, word_size);
}

static void emit_register_operand(char prefix, char opcode, int shift, int rnum)
{
    char *q = frag_more(prefix ? 2 : 1);
    if (prefix)
        *q++ = prefix;
    *q++ = opcode + (rnum << shift);
}

static void emit_indexed_operand(char prefix, char opcode, int shift, int rnum)
{
    char *q = frag_more(2);
    *q++ = (rnum & R_IX) ? 0xDD : 0xFD;
    *q = prefix ? prefix : (opcode + (6 << shift));
    
    expressionS offset;
    offset.X_op = O_symbol;
    offset.X_add_number = 0;
    emit_byte(&offset, BFD_RELOC_Z80_DISP8);
    
    if (prefix)
    {
        q = frag_more(1);
        *q = opcode + (6 << shift);
    }
}

static int process_register_operand(expressionS *arg, char *prefix)
{
    int rnum = arg->X_add_number;
    
    if (arg->X_md)
    {
        if (rnum != REG_HL)
        {
            ill_op();
            return -1;
        }
        return 6;
    }
    
    if ((*prefix == 0) && (rnum & R_INDEX))
    {
        *prefix = (rnum & R_IX) ? 0xDD : 0xFD;
        if (!(ins_ok & (INS_EZ80|INS_R800|INS_Z80N)))
            check_mach(INS_IDX_HALF);
        rnum &= ~R_INDEX;
    }
    
    if (rnum > 7)
    {
        ill_op();
        return -1;
    }
    
    return rnum;
}

static void emit_mx(char prefix, char opcode, int shift, expressionS *arg)
{
    int rnum = arg->X_add_number;
    
    switch (arg->X_op)
    {
    case O_register:
        rnum = process_register_operand(arg, &prefix);
        if (rnum >= 0)
            emit_register_operand(prefix, opcode, shift, rnum);
        break;
        
    case O_md1:
        if (ins_ok & INS_GBZ80)
            ill_op();
        else
            emit_indexed_operand(prefix, opcode, shift, rnum);
        break;
        
    default:
        abort();
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
  
  if (arg_m.X_op == O_md1 || arg_m.X_op == O_register)
    {
      emit_mx (prefix, opcode, 0, &arg_m);
    }
  else
    {
      ill_op ();
    }
    
  return p;
}

/* The operand m may be as above or one of the undocumented
   combinations (ix+d),r and (iy+d),r (if unportable instructions
   are allowed).  */

static const char *
emit_mr (char prefix, char opcode, const char *args)
{
  expressionS arg_m, arg_r;
  const char *p;

  p = parse_exp (args, &arg_m);

  switch (arg_m.X_op)
    {
    case O_md1:
      p = handle_md1_case(p, &arg_r, &opcode);
      emit_mx (prefix, opcode, 0, &arg_m);
      break;
    case O_register:
      emit_mx (prefix, opcode, 0, &arg_m);
      break;
    default:
      ill_op ();
    }
  return p;
}

static const char *
handle_md1_case(const char *p, expressionS *arg_r, char *opcode)
{
  if (*p != ',')
    return p;
  
  p = parse_exp (p + 1, arg_r);
  
  if (!is_valid_register(arg_r))
    {
      ill_op ();
      return p;
    }
  
  adjust_opcode_for_register(opcode, arg_r->X_add_number);
  check_instruction_compatibility();
  
  return p;
}

static int
is_valid_register(const expressionS *arg)
{
  return (arg->X_md == 0) && 
         (arg->X_op == O_register) && 
         (arg->X_add_number < 8);
}

static void
adjust_opcode_for_register(char *opcode, int reg_num)
{
  const int EMIT_MX_OFFSET = 6;
  *opcode += reg_num - EMIT_MX_OFFSET;
}

static void
check_instruction_compatibility(void)
{
  if (!(ins_ok & INS_Z80N))
    check_mach (INS_ROT_II_LD);
}

static void emit_prefix_and_opcode(char prefix, char opcode, char modifier)
{
  char *q = frag_more(prefix ? 2 : 1);
  if (prefix)
    *q++ = prefix;
  *q = opcode ^ modifier;
}

static void emit_sx(char prefix, char opcode, expressionS *arg_p)
{
  if (arg_p->X_op == O_register || arg_p->X_op == O_md1)
  {
    emit_mx(prefix, opcode, 0, arg_p);
    return;
  }
  
  if (arg_p->X_md)
  {
    ill_op();
    return;
  }
  
  emit_prefix_and_opcode(prefix, opcode, 0x46);
  emit_byte(arg_p, BFD_RELOC_8);
}

/* The operand s may be r, (hl), (ix+d), (iy+d), n.  */
static const char *
emit_s (char prefix, char opcode, const char *args)
{
  expressionS arg_s;
  const char *p;

  p = parse_exp (args, & arg_s);
  if (*p == ',' && arg_s.X_md == 0 && arg_s.X_op == O_register && arg_s.X_add_number == REG_A)
    {
      if (!(ins_ok & INS_EZ80) && !sdcc_compat)
        ill_op ();
      ++p;
      p = parse_exp (p, & arg_s);
    }
  emit_sx (prefix, opcode, & arg_s);
  return p;
}

static const char *
emit_sub (char prefix, char opcode, const char *args)
{
  expressionS arg_s;
  const char *p;

  if (!(ins_ok & INS_GBZ80))
    return emit_s (prefix, opcode, args);
  
  p = parse_exp (args, & arg_s);
  if (*p++ != ',')
    {
      error (_("bad instruction syntax"));
      return p;
    }

  if (arg_s.X_md != 0 || arg_s.X_op != O_register || arg_s.X_add_number != REG_A)
    ill_op ();

  p = parse_exp (p, & arg_s);
  emit_sx (prefix, opcode, & arg_s);
  return p;
}

static const char *
emit_swap (char prefix, char opcode, const char *args)
{
  if (!(ins_ok & INS_Z80N))
    return emit_mr (prefix, opcode, args);

  expressionS reg;
  const char *p = parse_exp (args, &reg);
  
  if (reg.X_md != 0 || reg.X_op != O_register || reg.X_add_number != REG_A)
    ill_op ();

  char *q = frag_more (2);
  *q++ = 0xED;
  *q = 0x23;
  
  return p;
}

static const char *
emit_call (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  expressionS addr;
  const char *p;

  p = parse_exp_not_indexed (args, &addr);
  if (addr.X_md)
    {
      ill_op ();
      return p;
    }
  
  char *q = frag_more (1);
  *q = opcode;
  emit_word (&addr);
  
  return p;
}

/* Operand may be rr, r, (hl), (ix+d), (iy+d).  */
static const char *
emit_incdec (char prefix, char opcode, const char * args)
{
  expressionS operand;
  const char *p;

  p = parse_exp (args, &operand);
  
  if (is_valid_register_operand(&operand)) {
    emit_register_instruction(prefix, operand.X_add_number);
  } else {
    emit_alternative_instruction(opcode, &operand);
  }
  
  return p;
}

static int is_valid_register_operand(const expressionS *operand)
{
  return (!operand->X_md) 
      && (operand->X_op == O_register) 
      && (R_ARITH & operand->X_add_number);
}

static void emit_register_instruction(char prefix, int rnum)
{
  char *q;
  int instruction_size = (rnum & R_INDEX) ? 2 : 1;
  
  q = frag_more(instruction_size);
  
  if (rnum & R_INDEX) {
    *q++ = get_index_prefix(rnum);
  }
  
  *q = prefix + ((rnum & 3) << 4);
}

static char get_index_prefix(int rnum)
{
  return (rnum & R_IX) ? 0xDD : 0xFD;
}

static void emit_alternative_instruction(char opcode, const expressionS *operand)
{
  if ((operand->X_op == O_md1) || (operand->X_op == O_register)) {
    emit_mx(0, opcode, 3, operand);
  } else {
    ill_op();
  }
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
      return p;
    }
  
  char *q = frag_more (1);
  *q = opcode;
  addr.X_add_number--;
  emit_byte (&addr, BFD_RELOC_8_PCREL);
  
  return p;
}

static const char *
emit_jp (char prefix, char opcode, const char * args)
{
  expressionS addr;
  const char *p;

  p = parse_exp_not_indexed (args, & addr);
  
  if (addr.X_md)
    {
      handle_register_jump(prefix, &addr);
    }
  else
    {
      emit_standard_jump(opcode, &addr);
    }
  
  return p;
}

static void handle_register_jump(char prefix, expressionS *addr)
{
  int rnum = addr->X_add_number;
  
  if (is_hl_register(addr, rnum))
    {
      emit_hl_jump(prefix, rnum);
    }
  else if (is_c_register_z80n(addr, rnum))
    {
      emit_c_register_jump();
    }
  else
    {
      ill_op();
    }
}

static int is_hl_register(expressionS *addr, int rnum)
{
  return (O_register == addr->X_op) && (REG_HL == (rnum & ~R_INDEX));
}

static int is_c_register_z80n(expressionS *addr, int rnum)
{
  return addr->X_op == O_register && rnum == REG_C && (ins_ok & INS_Z80N);
}

static void emit_hl_jump(char prefix, int rnum)
{
  char *q;
  
  if (rnum & R_INDEX)
    {
      q = frag_more(2);
      *q++ = (rnum & R_IX) ? 0xDD : 0xFD;
      *q = prefix;
    }
  else
    {
      q = frag_more(1);
      *q = prefix;
    }
}

static void emit_c_register_jump(void)
{
  char *q = frag_more(2);
  *q++ = 0xED;
  *q = 0x98;
}

static void emit_standard_jump(char opcode, expressionS *addr)
{
  char *q = frag_more(1);
  *q = opcode;
  emit_word(addr);
}

static const char *
emit_im (char prefix, char opcode, const char * args)
{
  expressionS mode;
  const char *p;

  p = parse_exp (args, & mode);
  
  if (!is_valid_mode(&mode))
    {
      ill_op ();
      return p;
    }
    
  process_im_mode(prefix, opcode, &mode);
  return p;
}

static int
is_valid_mode(const expressionS *mode)
{
  return !mode->X_md && mode->X_op == O_constant;
}

static void
process_im_mode(char prefix, char opcode, expressionS *mode)
{
  char *q;
  
  switch (mode->X_add_number)
    {
    case 1:
    case 2:
      ++mode->X_add_number;
      /* Fall through.  */
    case 0:
      q = frag_more (2);
      *q++ = prefix;
      *q = opcode + 8 * mode->X_add_number;
      break;
    default:
      ill_op ();
    }
}

static const char *emit_pop(char prefix ATTRIBUTE_UNUSED, char opcode, const char *args)
{
    expressionS regp;
    const char *p;

    p = parse_exp(args, &regp);
    
    if (!is_valid_pop_register(&regp)) {
        ill_op();
        return p;
    }

    emit_pop_instruction(opcode, regp.X_add_number);
    return p;
}

static int is_valid_pop_register(const expressionS *regp)
{
    return (!regp->X_md) 
           && (regp->X_op == O_register) 
           && (regp->X_add_number & R_STACKABLE);
}

static void emit_pop_instruction(char opcode, int rnum)
{
    char *q;
    
    if (rnum & R_INDEX) {
        q = frag_more(2);
        *q++ = (rnum & R_IX) ? 0xDD : 0xFD;
    } else {
        q = frag_more(1);
    }
    
    *q = opcode + ((rnum & 3) << 4);
}

static const char *
emit_push (char prefix, char opcode, const char * args)
{
  expressionS arg;
  const char *p;

  p = parse_exp (args, & arg);
  
  if (arg.X_op == O_register)
    return emit_pop (prefix, opcode, args);

  if (arg.X_md || arg.X_op == O_md1 || !(ins_ok & INS_Z80N))
    ill_op ();

  emit_instruction_bytes();
  emit_fixup(&arg);

  return p;
}

static void emit_instruction_bytes(void)
{
  char *q = frag_more (2);
  *q++ = 0xED;
  *q = 0x8A;
}

static void emit_fixup(expressionS *arg)
{
  char *q = frag_more (2);
  fix_new_exp (frag_now, q - frag_now->fr_literal, 2, arg, false,
               BFD_RELOC_Z80_16_BE);
}

static const char *
emit_retcc (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  char cc;
  const char *p = parse_cc (args, &cc);
  char *q = frag_more (1);
  
  *q = p ? (opcode + cc) : prefix;
  
  return p ? p : args;
}

static const char *
parse_and_validate_first_operand(const char *args, expressionS *term)
{
  const char *p = parse_exp(args, term);
  if (*p++ != ',')
    {
      error(_("bad instruction syntax"));
      return p;
    }
  
  if ((term->X_md) || (term->X_op != O_register))
    ill_op();
  
  return p;
}

static void
emit_adc_reg_a(char prefix, const char *p)
{
  emit_s(0, prefix, p);
}

static void
emit_adc_reg_hl(char opcode, const char *p)
{
  expressionS term;
  parse_exp(p, &term);
  
  if ((term.X_md) || (term.X_op != O_register))
    {
      ill_op();
      return;
    }
  
  int rnum = term.X_add_number;
  if (R_ARITH != (rnum & (R_ARITH | R_INDEX)))
    {
      ill_op();
      return;
    }
  
  char *q = frag_more(2);
  *q++ = 0xED;
  *q = opcode + ((rnum & 3) << 4);
}

static const char *
emit_adc(char prefix, char opcode, const char *args)
{
  expressionS term;
  const char *p = parse_and_validate_first_operand(args, &term);
  
  switch (term.X_add_number)
    {
    case REG_A:
      p = emit_s(0, prefix, p);
      break;
    case REG_HL:
      emit_adc_reg_hl(opcode, p);
      break;
    default:
      ill_op();
    }
  
  return p;
}

static int get_register_encoding(int reg)
{
  return reg & 3;
}

static int is_index_register(int reg)
{
  return reg & R_INDEX;
}

static int is_valid_arithmetic_register(int rhs, int lhs)
{
  return (rhs & R_ARITH) && (rhs == lhs || (rhs & ~R_INDEX) != REG_HL);
}

static void emit_index_prefix(char **q, int lhs)
{
  if (is_index_register(lhs))
    *(*q)++ = (lhs & R_IX) ? 0xDD : 0xFD;
}

static const char* handle_reg_a(char prefix, const char *p)
{
  return emit_s(0, prefix, p);
}

static const char* handle_reg_sp(const char *p)
{
  expressionS term;
  char *q;
  
  p = parse_exp(p, &term);
  if (!(ins_ok & INS_GBZ80) || term.X_md || term.X_op == O_register)
    ill_op();
  q = frag_more(1);
  *q = 0xE8;
  emit_byte(&term, BFD_RELOC_Z80_DISP8);
  return p;
}

static const char* emit_register_arithmetic(int lhs, int rhs, char opcode)
{
  char *q = frag_more(is_index_register(lhs) ? 2 : 1);
  emit_index_prefix(&q, lhs);
  *q = opcode + (get_register_encoding(rhs) << 4);
  return NULL;
}

static const char* emit_z80n_add_register_a(int lhs)
{
  char *q = frag_more(2);
  *q++ = 0xED;
  *q = 0x33 - get_register_encoding(lhs);
  return NULL;
}

static const char* emit_z80n_add_immediate(int lhs, expressionS *term)
{
  char *q = frag_more(2);
  *q++ = 0xED;
  *q = 0x36 - get_register_encoding(lhs);
  emit_word(term);
  return NULL;
}

static const char* handle_arithmetic_registers(int lhs, const char *p, char opcode)
{
  expressionS term;
  int rhs;
  
  p = parse_exp(p, &term);
  rhs = term.X_add_number;
  
  if (term.X_md != 0 || term.X_op == O_md1)
  {
    ill_op();
    return p;
  }
  
  if (term.X_op == O_register && is_valid_arithmetic_register(rhs, lhs))
  {
    emit_register_arithmetic(lhs, rhs, opcode);
    return p;
  }
  
  if (!is_index_register(lhs) && (ins_ok & INS_Z80N))
  {
    if (term.X_op == O_register && rhs == REG_A)
    {
      emit_z80n_add_register_a(lhs);
      return p;
    }
    else if (term.X_op != O_register && term.X_op != O_md1)
    {
      emit_z80n_add_immediate(lhs, &term);
      return p;
    }
  }
  
  ill_op();
  return p;
}

static int is_z80n_compatible_register(int reg)
{
  return (reg == REG_BC || reg == REG_DE) && (ins_ok & INS_Z80N);
}

static const char *
emit_add (char prefix, char opcode, const char * args)
{
  expressionS term;
  const char *p;

  p = parse_exp (args, &term);
  if (*p++ != ',')
    {
      error (_("bad instruction syntax"));
      return p;
    }

  if ((term.X_md) || (term.X_op != O_register))
    {
      ill_op ();
      return p;
    }

  switch (term.X_add_number)
    {
    case REG_A:
      return handle_reg_a(prefix, p);
      
    case REG_SP:
      return handle_reg_sp(p);
      
    case REG_BC:
    case REG_DE:
      if (!is_z80n_compatible_register(term.X_add_number))
        {
          ill_op ();
          return p;
        }
    case REG_HL:
    case REG_IX:
    case REG_IY:
      return handle_arithmetic_registers(term.X_add_number, p, opcode);
      
    default:
      ill_op ();
      return p;
    }
}

static const char *
emit_bit (char prefix, char opcode, const char * args)
{
  expressionS b;
  const char *p;

  p = parse_exp (args, &b);
  if (*p++ != ',')
    error (_("bad instruction syntax"));

  if (!is_valid_bit_number(&b))
    {
      ill_op ();
      return p;
    }

  int bit_shifted = b.X_add_number << 3;
  
  if (is_bit_instruction(opcode))
    p = emit_m (prefix, opcode + bit_shifted, p);
  else
    p = emit_mr (prefix, opcode + bit_shifted, p);
    
  return p;
}

static int is_valid_bit_number(const expressionS *b)
{
  return (!b->X_md) 
         && (b->X_op == O_constant)
         && (b->X_add_number >= 0)
         && (b->X_add_number < 8);
}

static int is_bit_instruction(char opcode)
{
  return opcode == 0x40;
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
  
  if (!is_register_de(&r1) || !is_register_b(&r2))
    ill_op ();
    
  q = frag_more (2);
  *q++ = prefix;
  *q = opcode;
  return p;
}

static int is_register_de(const expressionS *expr)
{
  return !expr->X_md && 
         expr->X_op == O_register && 
         expr->X_add_number == REG_DE;
}

static int is_register_b(const expressionS *expr)
{
  return !expr->X_md && 
         expr->X_op == O_register && 
         expr->X_add_number == REG_B;
}

static const char *
emit_jpcc (char prefix, char opcode, const char * args)
{
  char cc;
  const char *p;

  p = parse_cc (args, & cc);
  if (!p || *p != ',')
    return emit_jpcc_default(prefix, args);
  
  p++;
  return emit_call (0, opcode + cc, p);
}

static const char *
emit_jpcc_default(char prefix, const char * args)
{
  const char UNCONDITIONAL_JP_PREFIX = 0xC3;
  const char INDIRECT_JP_OPCODE = 0xE9;
  
  if (prefix == UNCONDITIONAL_JP_PREFIX)
    return emit_jp (INDIRECT_JP_OPCODE, prefix, args);
  
  return emit_call (0, prefix, args);
}

static const char *
emit_jrcc (char prefix, char opcode, const char * args)
{
  char cc;
  const char *p;

  p = parse_cc (args, &cc);
  if (!p || *p++ != ',')
    return emit_jr (0, prefix, args);

  #define MAX_CC_VALUE (3 << 3)
  if (cc > MAX_CC_VALUE)
    {
      error (_("condition code invalid for jr"));
      return p;
    }

  return emit_jr (0, opcode + cc, p);
}

static const char *parse_comma_separator(const char *p)
{
  p = skip_space(p);
  if (*p++ != ',')
    {
      error(_("bad instruction syntax"));
    }
  return p;
}

static int check_af_register(const char **p)
{
  if (TOLOWER(*(*p)++) == 'a' && TOLOWER(*(*p)++) == 'f')
    {
      if (**p == '`')
        ++(*p);
      return 0x08;
    }
  return 0;
}

static int check_hl_register(const char **p)
{
  if (TOLOWER(*(*p)++) == 'h' && TOLOWER(*(*p)++) == 'l')
    return 0xEB;
  return 0;
}

static int process_sp_register(const char **p, char *prefix)
{
  expressionS op;
  *p = parse_exp(*p, &op);
  if (op.X_op == O_register && op.X_md == 0 && 
      (op.X_add_number & ~R_INDEX) == REG_HL)
    {
      if (R_INDEX & op.X_add_number)
        *prefix = (R_IX & op.X_add_number) ? 0xDD : 0xFD;
      return 0xE3;
    }
  return 0;
}

static int determine_opcode(expressionS *op, const char **p, char *prefix)
{
  if (op->X_op != O_register)
    return 0;
    
  int reg_value = op->X_add_number | (op->X_md ? 0x8000 : 0);
  
  switch (reg_value)
    {
    case REG_AF:
      return check_af_register(p);
    case REG_DE:
      return check_hl_register(p);
    case REG_SP|0x8000:
      return process_sp_register(p, prefix);
    default:
      return 0;
    }
}

static const char *
emit_ex (char prefix_in ATTRIBUTE_UNUSED,
         char opcode_in ATTRIBUTE_UNUSED, const char * args)
{
  expressionS op;
  const char * p;
  char prefix = 0, opcode = 0;

  p = parse_exp_not_indexed(args, &op);
  p = parse_comma_separator(p);
  
  opcode = determine_opcode(&op, &p, &prefix);
  
  if (opcode)
    emit_insn(prefix, opcode, p);
  else
    ill_op();

  return p;
}

static const char *parse_register_and_port(const char *args, expressionS *reg, expressionS *port) {
    const char *p = parse_exp(args, reg);
    
    if (reg->X_md && reg->X_op == O_register && reg->X_add_number == REG_C) {
        *port = *reg;
        reg->X_md = 0;
        reg->X_add_number = REG_F;
        return p;
    }
    
    if (*p++ != ',') {
        error(_("bad instruction syntax"));
        return p;
    }
    
    return parse_exp(p, port);
}

static int is_valid_register(const expressionS *reg) {
    return reg->X_md == 0 && 
           reg->X_op == O_register && 
           (reg->X_add_number <= 7 || reg->X_add_number == REG_F);
}

static void emit_port_instruction(const expressionS *reg, const expressionS *port) {
    if (REG_A != reg->X_add_number) {
        ill_op();
        return;
    }
    
    char *q = frag_more(1);
    *q = 0xDB;
    emit_byte(port, BFD_RELOC_8);
}

static void validate_port_c_or_bc(const expressionS *reg, const expressionS *port) {
    if (port->X_add_number == REG_BC && !(ins_ok & INS_EZ80)) {
        ill_op();
    } else if (reg->X_add_number == REG_F && !(ins_ok & (INS_R800|INS_Z80N))) {
        check_mach(INS_IN_F_C);
    }
}

static void emit_register_instruction(const expressionS *reg, const expressionS *port) {
    if (port->X_add_number != REG_C && port->X_add_number != REG_BC) {
        ill_op();
        return;
    }
    
    validate_port_c_or_bc(reg, port);
    
    char *q = frag_more(2);
    *q++ = 0xED;
    *q = 0x40 | ((reg->X_add_number & 7) << 3);
}

static const char *emit_in(char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED, const char *args) {
    expressionS reg, port;
    
    const char *p = parse_register_and_port(args, &reg, &port);
    
    if (!is_valid_register(&reg) || !port.X_md) {
        ill_op();
        return p;
    }
    
    if (port.X_op != O_md1 && port.X_op != O_register) {
        emit_port_instruction(&reg, &port);
    } else {
        emit_register_instruction(&reg, &port);
    }
    
    return p;
}

static const char *
emit_in0 (char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED,
        const char * args)
{
  expressionS reg, port;
  const char *p;

  p = parse_exp (args, &reg);
  if (*p++ != ',')
    {
      error (_("bad instruction syntax"));
      return p;
    }

  p = parse_exp (p, &port);
  
  if (is_valid_in0_instruction(&reg, &port))
    {
      emit_in0_instruction(&reg, &port);
    }
  else
    {
      ill_op ();
    }
    
  return p;
}

static int
is_valid_in0_instruction(const expressionS *reg, const expressionS *port)
{
  return reg->X_md == 0
      && reg->X_op == O_register
      && reg->X_add_number <= 7
      && port->X_md
      && port->X_op != O_md1
      && port->X_op != O_register;
}

static void
emit_in0_instruction(const expressionS *reg, const expressionS *port)
{
  char *q = frag_more (2);
  *q++ = 0xED;
  *q = 0x00 | (reg->X_add_number << 3);
  emit_byte (port, BFD_RELOC_8);
}

static const char *
emit_out (char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED,
	 const char * args)
{
  expressionS reg, port;
  const char *p;

  p = parse_exp (args, & port);
  if (*p++ != ',')
    {
      error (_("bad instruction syntax"));
      return p;
    }
  p = parse_exp (p, &reg);
  
  if (!port.X_md)
    { 
      ill_op (); 
      return p; 
    }
  
  if (reg.X_op == O_constant && reg.X_add_number == 0)
    {
      if (!(ins_ok & INS_Z80N))
	check_mach (INS_OUT_C_0);
      reg.X_op = O_register;
      reg.X_add_number = 6;
    }
  
  if (reg.X_md || reg.X_op != O_register || reg.X_add_number > 7)
    {
      ill_op ();
      return p;
    }
  
  if (port.X_op != O_register && port.X_op != O_md1)
    {
      if (REG_A == reg.X_add_number)
        {
          char *q = frag_more (1);
          *q = 0xD3;
          emit_byte (&port, BFD_RELOC_8);
        }
      else
        {
          ill_op ();
        }
      return p;
    }
  
  if (REG_C != port.X_add_number && port.X_add_number != REG_BC)
    {
      ill_op ();
      return p;
    }
  
  if (port.X_add_number == REG_BC && !(ins_ok & INS_EZ80))
    {
      ill_op ();
      return p;
    }
  
  char *q = frag_more (2);
  *q++ = 0xED;
  *q = 0x41 | (reg.X_add_number << 3);
  
  return p;
}

static const char *parse_port_expression(const char *args, expressionS *port)
{
    return parse_exp(args, port);
}

static const char *parse_register_expression(const char *p, expressionS *reg)
{
    return parse_exp(p, reg);
}

static int is_valid_port(const expressionS *port)
{
    return port->X_md != 0 
        && port->X_op != O_register 
        && port->X_op != O_md1;
}

static int is_valid_register(const expressionS *reg)
{
    const int MAX_REGISTER_NUMBER = 7;
    return reg->X_md == 0 
        && reg->X_op == O_register 
        && reg->X_add_number <= MAX_REGISTER_NUMBER;
}

static void emit_out0_instruction(const expressionS *reg, const expressionS *port)
{
    const unsigned char ED_PREFIX = 0xED;
    const unsigned char BASE_OPCODE = 0x01;
    const int REGISTER_SHIFT = 3;
    
    char *q = frag_more(2);
    *q++ = ED_PREFIX;
    *q = BASE_OPCODE | (reg->X_add_number << REGISTER_SHIFT);
    emit_byte(port, BFD_RELOC_8);
}

static const char *emit_out0(char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED,
                             const char *args)
{
    expressionS reg, port;
    const char *p;
    
    p = parse_port_expression(args, &port);
    if (*p++ != ',')
    {
        error(_("bad instruction syntax"));
        return p;
    }
    
    p = parse_register_expression(p, &reg);
    
    if (is_valid_port(&port) && is_valid_register(&reg))
    {
        emit_out0_instruction(&reg, &port);
    }
    else
    {
        ill_op();
    }
    
    return p;
}

static const char *
emit_rst (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  #define RST_ADDRESS_MASK (7 << 3)
  
  expressionS addr;
  const char *p;

  p = parse_exp_not_indexed (args, &addr);
  
  if (addr.X_op != O_constant)
    {
      error ("rst needs constant address");
      return p;
    }

  if (addr.X_add_number & ~RST_ADDRESS_MASK)
    {
      ill_op ();
      return p;
    }
  
  char *q = frag_more (1);
  *q = opcode + (addr.X_add_number & RST_ADDRESS_MASK);
  
  return p;
}

/* For 8-bit indirect load to memory instructions like: LD (HL),n or LD (ii+d),n.  */
static void
emit_ld_m_n (expressionS *dst, expressionS *src)
{
  char prefix = get_register_prefix(dst->X_add_number);
  
  if (prefix == INVALID_PREFIX)
    {
      ill_op ();
      return;
    }

  emit_instruction_bytes(prefix);
  
  if (prefix)
    emit_displacement_offset(dst);
    
  emit_byte (src, BFD_RELOC_8);
}

static char
get_register_prefix (int reg)
{
  switch (reg)
    {
    case REG_HL: return 0x00;
    case REG_IX: return 0xDD;
    case REG_IY: return 0xFD;
    default: return INVALID_PREFIX;
    }
}

static void
emit_instruction_bytes (char prefix)
{
  char *q = frag_more (prefix ? 2 : 1);
  
  if (prefix)
    *q++ = prefix;
    
  *q = 0x36;
}

static void
emit_displacement_offset (expressionS *dst)
{
  expressionS dst_offset = *dst;
  dst_offset.X_op = O_symbol;
  dst_offset.X_add_number = 0;
  emit_byte (&dst_offset, BFD_RELOC_Z80_DISP8);
}

#define INVALID_PREFIX -1

/* For 8-bit load register to memory instructions: LD (<expression>),r.  */
static void emit_gbz80_hl_increment(expressionS *dst, expressionS *src)
{
    if (src->X_op != O_register || src->X_add_number != REG_A)
        return;
    *frag_more(1) = (dst->X_add_number == REG_HL) ? 0x22 : 0x32;
}

static void emit_indirect_bc_de(expressionS *dst, expressionS *src)
{
    if (src->X_add_number != REG_A)
        return;
    char *q = frag_more(1);
    *q = 0x02 | ((dst->X_add_number & 3) << 4);
}

static void emit_indirect_hl_indexed(expressionS *dst, expressionS *src, char prefix)
{
    if (src->X_add_number > 7)
        return;
    
    char *q = frag_more(prefix ? 2 : 1);
    if (prefix)
        *q++ = prefix;
    *q = 0x70 | src->X_add_number;
    
    if (prefix)
    {
        expressionS dst_offset = *dst;
        dst_offset.X_op = O_symbol;
        dst_offset.X_add_number = 0;
        emit_byte(&dst_offset, BFD_RELOC_Z80_DISP8);
    }
}

static void emit_indirect_absolute(expressionS *dst, expressionS *src)
{
    if (src->X_add_number != REG_A)
        return;
    char *q = frag_more(1);
    *q = (ins_ok & INS_GBZ80) ? 0xEA : 0x32;
    emit_word(dst);
}

static void handle_md1_case(expressionS *dst, expressionS *src)
{
    if (ins_ok & INS_GBZ80)
    {
        emit_gbz80_hl_increment(dst, src);
        return;
    }
    
    char prefix = (dst->X_add_number == REG_IX) ? 0xDD : 0xFD;
    
    switch (dst->X_add_number)
    {
    case REG_BC:
    case REG_DE:
        emit_indirect_bc_de(dst, src);
        return;
    case REG_IX:
    case REG_IY:
    case REG_HL:
        emit_indirect_hl_indexed(dst, src, prefix);
        return;
    }
}

static void handle_register_case(expressionS *dst, expressionS *src)
{
    switch (dst->X_add_number)
    {
    case REG_BC:
    case REG_DE:
        emit_indirect_bc_de(dst, src);
        return;
    case REG_IX:
    case REG_IY:
    case REG_HL:
        emit_indirect_hl_indexed(dst, src, 0);
        return;
    }
}

static void emit_ld_m_r(expressionS *dst, expressionS *src)
{
    switch (dst->X_op)
    {
    case O_md1:
        handle_md1_case(dst, src);
        return;
    case O_register:
        handle_register_case(dst, src);
        return;
    default:
        emit_indirect_absolute(dst, src);
        return;
    }
    ill_op();
}

/* For 16-bit load register to memory instructions: LD (<expression>),rr.  */
static int get_prefix_for_register(int reg, int is_ez80_indirect) {
    if (is_ez80_indirect) {
        switch (reg) {
        case REG_IX: return 0xDD;
        case REG_IY: return 0xFD;
        case REG_HL: return 0xED;
        default: ill_op(); return 0;
        }
    }
    switch (reg) {
    case REG_BC: return 0xED;
    case REG_DE: return 0xED;
    case REG_HL: return 0x00;
    case REG_IX: return 0xDD;
    case REG_IY: return 0xFD;
    case REG_SP: return 0xED;
    default: ill_op(); return 0;
    }
}

static int get_opcode_for_ez80_indirect(int src_reg, int prefix) {
    switch (src_reg) {
    case REG_BC: return 0x0F;
    case REG_DE: return 0x1F;
    case REG_HL: return 0x2F;
    case REG_IX: return (prefix != 0xFD) ? 0x3F : 0x3E;
    case REG_IY: return (prefix != 0xFD) ? 0x3E : 0x3F;
    default: ill_op(); return 0;
    }
}

static int get_opcode_for_direct(int src_reg) {
    switch (src_reg) {
    case REG_BC: return 0x43;
    case REG_DE: return 0x53;
    case REG_HL: return 0x22;
    case REG_IX: return 0x22;
    case REG_IY: return 0x22;
    case REG_SP: return 0x73;
    default: ill_op(); return 0;
    }
}

static void emit_prefix_and_opcode(int prefix, int opcode) {
    char *q = frag_more(prefix ? 2 : 1);
    if (prefix) *q++ = prefix;
    *q = opcode;
}

static void emit_offset_if_needed(expressionS *dst, int prefix) {
    if (prefix == 0xFD || prefix == 0xDD) {
        expressionS dst_offset = *dst;
        dst_offset.X_op = O_symbol;
        dst_offset.X_add_number = 0;
        emit_byte(&dst_offset, BFD_RELOC_Z80_DISP8);
    }
}

static void handle_ez80_indirect(expressionS *dst, expressionS *src) {
    if (!(ins_ok & INS_EZ80)) ill_op();
    
    int prefix = get_prefix_for_register(dst->X_add_number, 1);
    int opcode = get_opcode_for_ez80_indirect(src->X_add_number, prefix);
    
    emit_prefix_and_opcode(prefix, opcode);
    emit_offset_if_needed(dst, prefix);
}

static void handle_gbz80_direct(expressionS *dst, expressionS *src) {
    if (src->X_add_number != REG_SP) ill_op();
    
    emit_prefix_and_opcode(0x00, 0x08);
    emit_word(dst);
}

static void handle_standard_direct(expressionS *dst, expressionS *src) {
    int prefix = get_prefix_for_register(src->X_add_number, 0);
    int opcode = get_opcode_for_direct(src->X_add_number);
    
    emit_prefix_and_opcode(prefix, opcode);
    emit_word(dst);
}

static void emit_ld_m_rr(expressionS *dst, expressionS *src) {
    switch (dst->X_op) {
    case O_md1:
    case O_register:
        handle_ez80_indirect(dst, src);
        break;
    default:
        if (ins_ok & INS_GBZ80)
            handle_gbz80_direct(dst, src);
        else
            handle_standard_direct(dst, src);
        break;
    }
}

static void emit_ld_a_bc_de(expressionS *src)
{
    char opcode = 0;
    
    switch (src->X_add_number)
    {
        case REG_BC: opcode = 0x0A; break;
        case REG_DE: opcode = 0x1A; break;
        default: return;
    }
    
    char *q = frag_more(1);
    *q = opcode;
}

static void emit_ld_a_hl_inc_dec(expressionS *dst, expressionS *src)
{
    if (dst->X_op == O_register && dst->X_add_number == REG_A)
        *frag_more(1) = (src->X_add_number == REG_HL) ? 0x2A : 0x3A;
    else
        ill_op();
}

static char get_prefix_for_register(int reg_number)
{
    switch (reg_number)
    {
        case REG_HL: return 0x00;
        case REG_IX: return 0xDD;
        case REG_IY: return 0xFD;
        default:
            ill_op();
            return 0;
    }
}

static void emit_ld_r_indirect(expressionS *dst, expressionS *src)
{
    if (dst->X_add_number > 7)
        ill_op();
    
    char prefix = get_prefix_for_register(src->X_add_number);
    char opcode = 0x46;
    
    char *q = frag_more(prefix ? 2 : 1);
    if (prefix)
        *q++ = prefix;
    *q = opcode | ((dst->X_add_number & 7) << 3);
    
    if (prefix)
    {
        expressionS src_offset = *src;
        src_offset.X_op = O_symbol;
        src_offset.X_add_number = 0;
        emit_byte(&src_offset, BFD_RELOC_Z80_DISP8);
    }
}

static void emit_ld_a_direct(expressionS *src)
{
    char *q = frag_more(1);
    *q = (ins_ok & INS_GBZ80) ? 0xFA : 0x3A;
    emit_word(src);
}

static void emit_ld_r_m(expressionS *dst, expressionS *src)
{
    if (dst->X_add_number == REG_A && src->X_op == O_register)
    {
        emit_ld_a_bc_de(src);
        if (src->X_add_number == REG_BC || src->X_add_number == REG_DE)
            return;
    }

    switch (src->X_op)
    {
    case O_md1:
        if (ins_ok & INS_GBZ80)
        {
            emit_ld_a_hl_inc_dec(dst, src);
            break;
        }
        /* Fall through. */
    case O_register:
        emit_ld_r_indirect(dst, src);
        break;
    default:
        if (dst->X_add_number == REG_A)
            emit_ld_a_direct(src);
        else
            ill_op();
    }
}

static void
emit_prefix_if_needed(char *q, char prefix)
{
    if (prefix)
    {
        if (ins_ok & INS_GBZ80)
            ill_op();
        else if (!(ins_ok & (INS_EZ80|INS_R800|INS_Z80N)))
            check_mach(INS_IDX_HALF);
        *q = prefix;
    }
}

static char
get_prefix_for_register(int reg)
{
    if (reg == (REG_H|R_IX) || reg == (REG_L|R_IX))
        return 0xDD;
    if (reg == (REG_H|R_IY) || reg == (REG_L|R_IY))
        return 0xFD;
    return 0;
}

static int
is_valid_8bit_register(int reg)
{
    return (reg == REG_A || reg == REG_B || reg == REG_C ||
            reg == REG_D || reg == REG_E || reg == REG_H || 
            reg == REG_L);
}

static void
emit_ld_r_n(expressionS *dst, expressionS *src)
{
    char *q;
    char prefix = get_prefix_for_register(dst->X_add_number);
    
    if (!prefix && !is_valid_8bit_register(dst->X_add_number))
        ill_op();
    
    q = frag_more(prefix ? 2 : 1);
    emit_prefix_if_needed(q, prefix);
    if (prefix)
        q++;
    
    *q = 0x06 | ((dst->X_add_number & 7) << 3);
    emit_byte(src, BFD_RELOC_8);
}

static int get_sp_source_prefix(int src_reg) {
    switch (src_reg) {
        case REG_HL: return 0x00;
        case REG_IX: return 0xDD;
        case REG_IY: return 0xFD;
        default: return -1;
    }
}

static void handle_sp_destination(expressionS *src, int *prefix, int *opcode) {
    *prefix = get_sp_source_prefix(src->X_add_number);
    if (*prefix == -1)
        ill_op();
    *opcode = 0xF9;
}

static void handle_hl_destination(expressionS *src, int *prefix, int *opcode) {
    if (!(ins_ok & INS_EZ80))
        ill_op();
    if (src->X_add_number != REG_I)
        ill_op();
    if (cpu_mode < 1)
        error(_("ADL mode instruction"));
    *prefix = 0xED;
    *opcode = 0xD7;
}

static void handle_i_destination(expressionS *src, int *prefix, int *opcode) {
    if (src->X_add_number == REG_HL) {
        if (!(ins_ok & INS_EZ80))
            ill_op();
        if (cpu_mode < 1)
            error(_("ADL mode instruction"));
        *prefix = 0xED;
        *opcode = 0xC7;
    } else if (src->X_add_number == REG_A) {
        *prefix = 0xED;
        *opcode = 0x47;
    } else {
        ill_op();
    }
}

static void handle_mb_destination(expressionS *src, int *prefix, int *opcode) {
    if (!(ins_ok & INS_EZ80) || (src->X_add_number != REG_A))
        ill_op();
    if (cpu_mode < 1)
        error(_("ADL mode instruction"));
    *prefix = 0xED;
    *opcode = 0x6D;
}

static void handle_r_destination(expressionS *src, int *prefix, int *opcode) {
    if (src->X_add_number == REG_A) {
        *prefix = 0xED;
        *opcode = 0x4F;
    } else {
        ill_op();
    }
}

static int handle_a_destination(expressionS *src, int *prefix, int *opcode) {
    if (src->X_add_number == REG_I) {
        *prefix = 0xED;
        *opcode = 0x57;
        return 1;
    }
    if (src->X_add_number == REG_R) {
        *prefix = 0xED;
        *opcode = 0x5F;
        return 1;
    }
    if (src->X_add_number == REG_MB) {
        if (!(ins_ok & INS_EZ80)) {
            ill_op();
        } else {
            if (cpu_mode < 1)
                error(_("ADL mode instruction"));
            *prefix = 0xED;
            *opcode = 0x6E;
        }
        return 1;
    }
    return 0;
}

static void validate_source_register(expressionS *src, int prefix, int dst_reg) {
    switch (src->X_add_number) {
        case REG_A:
        case REG_B:
        case REG_C:
        case REG_D:
        case REG_E:
            break;
        case REG_H:
        case REG_L:
            if (prefix != 0)
                ill_op();
            break;
        case REG_H|R_IX:
        case REG_L|R_IX:
            if (prefix == 0xFD || dst_reg == REG_H || dst_reg == REG_L)
                ill_op();
            break;
        case REG_H|R_IY:
        case REG_L|R_IY:
            if (prefix == 0xDD || dst_reg == REG_H || dst_reg == REG_L)
                ill_op();
            break;
        default:
            ill_op();
    }
}

static int get_source_prefix(expressionS *src, int current_prefix) {
    if ((src->X_add_number & (R_IX | R_IY)) == R_IX)
        return 0xDD;
    if ((src->X_add_number & (R_IX | R_IY)) == R_IY)
        return 0xFD;
    return current_prefix;
}

#define SIS_PREFIX 0x40
#define LIS_PREFIX 0x49
#define SIL_PREFIX 0x52
#define LIL_PREFIX 0x5B

static int adjust_ez80_opcode(int opcode) {
    switch (opcode) {
        case SIS_PREFIX:
        case LIS_PREFIX:
        case SIL_PREFIX:
        case LIL_PREFIX:
            as_warn(_("unsupported instruction, assembled as NOP"));
            return 0x00;
        default:
            return opcode;
    }
}

static void emit_instruction(int prefix, int opcode) {
    char *q = frag_more(prefix ? 2 : 1);
    if (prefix)
        *q++ = prefix;
    *q = opcode;
}

static void emit_ld_r_r(expressionS *dst, expressionS *src) {
    int prefix = 0;
    int opcode = 0;
    int ii_halves = 0;

    switch (dst->X_add_number) {
        case REG_SP:
            handle_sp_destination(src, &prefix, &opcode);
            break;
        case REG_HL:
            handle_hl_destination(src, &prefix, &opcode);
            break;
        case REG_I:
            handle_i_destination(src, &prefix, &opcode);
            break;
        case REG_MB:
            handle_mb_destination(src, &prefix, &opcode);
            break;
        case REG_R:
            handle_r_destination(src, &prefix, &opcode);
            break;
        case REG_A:
            if (!handle_a_destination(src, &prefix, &opcode))
                prefix = 0x00;
            break;
        case REG_B:
        case REG_C:
        case REG_D:
        case REG_E:
        case REG_H:
        case REG_L:
            prefix = 0x00;
            break;
        case REG_H|R_IX:
        case REG_L|R_IX:
            prefix = 0xDD;
            ii_halves = 1;
            break;
        case REG_H|R_IY:
        case REG_L|R_IY:
            prefix = 0xFD;
            ii_halves = 1;
            break;
        default:
            ill_op();
    }

    if (opcode == 0) {
        validate_source_register(src, prefix, dst->X_add_number);
        prefix = get_source_prefix(src, prefix);
        if ((src->X_add_number & (R_IX | R_IY)) != 0)
            ii_halves = 1;
        opcode = 0x40 + ((dst->X_add_number & 7) << 3) + (src->X_add_number & 7);
    }

    if ((ins_ok & INS_GBZ80) && prefix != 0)
        ill_op();
    if (ii_halves && !(ins_ok & (INS_EZ80|INS_R800|INS_Z80N)))
        check_mach(INS_IDX_HALF);
    if (prefix == 0 && (ins_ok & INS_EZ80))
        opcode = adjust_ez80_opcode(opcode);

    emit_instruction(prefix, opcode);
}

static int get_indirect_load_opcode(int reg, int prefix)
{
    switch (reg)
    {
    case REG_BC: return 0x07;
    case REG_DE: return 0x17;
    case REG_HL: return 0x27;
    case REG_IX: return (prefix == 0xED || prefix == 0xDD) ? 0x37 : 0x31;
    case REG_IY: return (prefix == 0xED || prefix == 0xDD) ? 0x31 : 0x37;
    default: return -1;
    }
}

static void get_direct_load_prefix_opcode(int reg, int *prefix, int *opcode)
{
    switch (reg)
    {
    case REG_BC: *prefix = 0xED; *opcode = 0x4B; break;
    case REG_DE: *prefix = 0xED; *opcode = 0x5B; break;
    case REG_HL: *prefix = 0x00; *opcode = 0x2A; break;
    case REG_SP: *prefix = 0xED; *opcode = 0x7B; break;
    case REG_IX: *prefix = 0xDD; *opcode = 0x2A; break;
    case REG_IY: *prefix = 0xFD; *opcode = 0x2A; break;
    default: *prefix = -1; *opcode = -1; break;
    }
}

static void emit_indirect_load(expressionS *dst, expressionS *src, int prefix)
{
    char *q;
    int opcode;
    expressionS src_offset;

    if (!(ins_ok & INS_EZ80))
        ill_op();

    opcode = get_indirect_load_opcode(dst->X_add_number, prefix);
    if (opcode == -1)
        ill_op();

    q = frag_more(2);
    *q++ = prefix;
    *q = opcode;

    if (prefix != 0xED)
    {
        src_offset = *src;
        src_offset.X_op = O_symbol;
        src_offset.X_add_number = 0;
        emit_byte(&src_offset, BFD_RELOC_Z80_DISP8);
    }
}

static void emit_direct_load(expressionS *dst, expressionS *src)
{
    char *q;
    int prefix;
    int opcode;

    get_direct_load_prefix_opcode(dst->X_add_number, &prefix, &opcode);
    if (opcode == -1)
        ill_op();

    q = frag_more(prefix ? 2 : 1);
    if (prefix)
        *q++ = prefix;
    *q = opcode;
    emit_word(src);
}

static void
emit_ld_rr_m(expressionS *dst, expressionS *src)
{
    int prefix;

    if (ins_ok & INS_GBZ80)
        ill_op();

    switch (src->X_op)
    {
    case O_md1:
        prefix = (src->X_add_number == REG_IX) ? 0xDD : 0xFD;
        emit_indirect_load(dst, src, prefix);
        break;
    case O_register:
        emit_indirect_load(dst, src, 0xED);
        break;
    default:
        emit_direct_load(dst, src);
        break;
    }
}

static int get_prefix_for_register(int reg_number)
{
    if (reg_number == REG_IX)
        return 0xDD;
    if (reg_number == REG_IY)
        return 0xFD;
    return 0x00;
}

static int get_opcode_for_register(int reg_number)
{
    const int LD_HL_NN = 0x21;
    const int LD_RR_NN_BASE = 0x01;
    
    if (reg_number == REG_IX || reg_number == REG_IY || reg_number == REG_HL)
        return LD_HL_NN;
    
    if (reg_number == REG_BC || reg_number == REG_DE || reg_number == REG_SP)
        return LD_RR_NN_BASE + ((reg_number & 3) << 4);
    
    return -1;
}

static void validate_prefix_instruction(int prefix)
{
    if (prefix && (ins_ok & INS_GBZ80))
        ill_op();
}

static void emit_instruction_bytes(int prefix, int opcode)
{
    char *q;
    
    if (prefix)
    {
        q = frag_more(2);
        *q++ = prefix;
        *q = opcode;
    }
    else
    {
        q = frag_more(1);
        *q = opcode;
    }
}

static void emit_ld_rr_nn(expressionS *dst, expressionS *src)
{
    int prefix = get_prefix_for_register(dst->X_add_number);
    int opcode = get_opcode_for_register(dst->X_add_number);
    
    if (opcode == -1)
    {
        ill_op();
        return;
    }
    
    validate_prefix_instruction(prefix);
    emit_instruction_bytes(prefix, opcode);
    emit_word(src);
}

static const char *parse_ld_operands(const char *args, expressionS *dst, expressionS *src)
{
  const char *p = parse_exp(args, dst);
  if (*p++ != ',')
    error(_("bad instruction syntax"));
  return parse_exp(p, src);
}

static void emit_ld_from_memory(const expressionS *dst, const expressionS *src)
{
  if (src->X_op == O_register)
  {
    if (src->X_add_number <= 7)
      emit_ld_m_r(dst, src);
    else
      emit_ld_m_rr(dst, src);
  }
  else
  {
    emit_ld_m_n(dst, src);
  }
}

static void emit_ld_to_register(const expressionS *dst, const expressionS *src)
{
  if (src->X_md)
  {
    if (dst->X_add_number <= 7)
      emit_ld_r_m(dst, src);
    else
      emit_ld_rr_m(dst, src);
  }
  else if (src->X_op == O_register)
  {
    emit_ld_r_r(dst, src);
  }
  else if ((dst->X_add_number & ~R_INDEX) <= 7)
  {
    emit_ld_r_n(dst, src);
  }
  else
  {
    emit_ld_rr_nn(dst, src);
  }
}

static const char *
emit_ld(char prefix_in ATTRIBUTE_UNUSED, char opcode_in ATTRIBUTE_UNUSED,
        const char *args)
{
  expressionS dst, src;
  const char *p = parse_ld_operands(args, &dst, &src);

  if (dst.X_md)
    emit_ld_from_memory(&dst, &src);
  else if (dst.X_op == O_register)
    emit_ld_to_register(&dst, &src);
  else
    ill_op();

  return p;
}

static const char *
emit_lddldi (char prefix, char opcode, const char * args)
{
  expressionS dst, src;
  const char *p;
  char *q;

  if (!(ins_ok & INS_GBZ80))
    return emit_insn (prefix, opcode, args);

  p = parse_exp (args, & dst);
  if (*p++ != ',')
    error (_("bad instruction syntax"));
  p = parse_exp (p, & src);

  if (dst.X_op != O_register || src.X_op != O_register)
    ill_op ();

  opcode = (opcode & 0x08) * 2 + 0x22;

  if (dst.X_md != 0 && dst.X_add_number == REG_HL && 
      src.X_md == 0 && src.X_add_number == REG_A)
    opcode |= 0x00;
  else if (dst.X_md == 0 && dst.X_add_number == REG_A && 
           src.X_md != 0 && src.X_add_number == REG_HL)
    opcode |= 0x08;
  else
    ill_op ();

  q = frag_more (1);
  *q = opcode;
  return p;
}

static const char *parse_and_validate_args(const char *args, expressionS *dst, expressionS *src)
{
    const char *p = parse_exp(args, dst);
    if (*p++ != ',')
    {
        error(_("bad instruction syntax"));
        return p;
    }
    return parse_exp(p, src);
}

static void emit_load_a_from_memory(expressionS *src)
{
    if (src->X_op != O_register)
    {
        char *q = frag_more(1);
        *q = 0xF0;
        emit_byte(src, BFD_RELOC_8);
    }
    else if (src->X_add_number == REG_C)
    {
        *frag_more(1) = 0xF2;
    }
    else
    {
        ill_op();
    }
}

static void emit_store_a_to_memory(expressionS *dst)
{
    if (dst->X_op == O_register)
    {
        if (dst->X_add_number == REG_C)
        {
            char *q = frag_more(1);
            *q = 0xE2;
        }
        else
        {
            ill_op();
        }
    }
    else
    {
        char *q = frag_more(1);
        *q = 0xE0;
        emit_byte(dst, BFD_RELOC_8);
    }
}

static int is_register_a(expressionS *expr)
{
    return expr->X_md == 0 && expr->X_op == O_register && expr->X_add_number == REG_A;
}

static int is_memory_operand(expressionS *expr)
{
    return expr->X_md != 0 && expr->X_op != O_md1;
}

static const char *emit_ldh(char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED, const char *args)
{
    expressionS dst, src;
    
    const char *p = parse_and_validate_args(args, &dst, &src);
    
    if (is_register_a(&dst) && is_memory_operand(&src))
    {
        emit_load_a_from_memory(&src);
    }
    else if (is_memory_operand(&dst) && is_register_a(&src))
    {
        emit_store_a_to_memory(&dst);
    }
    else
    {
        ill_op();
    }
    
    return p;
}

static const char *parse_comma_separator(const char *p)
{
  if (*p++ != ',')
    {
      error (_("bad instruction syntax"));
    }
  return p;
}

static void validate_sp_register(const expressionS *exp)
{
  if (exp->X_md || exp->X_op != O_register || exp->X_add_number != REG_SP)
    {
      ill_op();
    }
}

static void validate_non_register_operand(const expressionS *exp)
{
  if (exp->X_md || exp->X_op == O_register || exp->X_op == O_md1)
    {
      ill_op();
    }
}

static void emit_opcode_byte(char opcode)
{
  char *q = frag_more(1);
  *q = opcode;
}

static const char *
emit_ldhl (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  expressionS dst, src;
  const char *p;
  
  p = parse_exp(args, &dst);
  p = parse_comma_separator(p);
  p = parse_exp(p, &src);
  
  validate_sp_register(&dst);
  validate_non_register_operand(&src);
  
  emit_opcode_byte(opcode);
  emit_byte(&src, BFD_RELOC_Z80_DISP8);
  
  return p;
}

static const char *
parse_lea_pea_args (const char * args, expressionS *op)
{
  const char *p = parse_exp (args, op);
  
  if (!sdcc_compat || *p != ',' || op->X_op != O_register)
    return p;
    
  expressionS off;
  p = parse_exp (p + 1, &off);
  op->X_op = O_add;
  op->X_add_symbol = make_expr_symbol (&off);
  
  return p;
}

static const char *
emit_lea (char prefix, char opcode, const char * args)
{
  expressionS dst, src;
  const char *p;
  char *q;
  int rnum;

  p = parse_exp (args, & dst);
  if (dst.X_md != 0 || dst.X_op != O_register)
    ill_op ();

  rnum = dst.X_add_number;
  opcode = get_dst_opcode(rnum);

  if (*p++ != ',')
    error (_("bad instruction syntax"));

  p = parse_lea_pea_args (p, & src);
  if (src.X_md != 0 || src.X_op != O_add)
    ill_op ();

  rnum = src.X_add_number;
  prepare_source(&src);
  opcode = adjust_opcode_for_source(opcode, rnum);

  q = frag_more (2);
  *q++ = prefix;
  *q = opcode;

  src.X_op = O_symbol;
  src.X_add_number = 0;
  emit_byte (& src, BFD_RELOC_Z80_DISP8);

  return p;
}

static char get_dst_opcode(int rnum)
{
  switch (rnum)
    {
    case REG_BC:
    case REG_DE:
    case REG_HL:
      return 0x02 | ((rnum & 0x03) << 4);
    case REG_IX:
      return 0x32;
    case REG_IY:
      return 0x33;
    default:
      ill_op ();
      return 0;
    }
}

static void prepare_source(expressionS *src)
{
  switch (src->X_op)
    {
    case O_add:
      break;
    case O_register:
      src->X_add_symbol = zero;
      break;
    default:
      ill_op ();
    }
}

static char adjust_opcode_for_source(char opcode, int rnum)
{
  switch (rnum)
    {
    case REG_IX:
      return opcode == 0x33 ? 0x55 : opcode | 0x00;
    case REG_IY:
      return opcode == 0x32 ? 0x54 : opcode | 0x01;
    default:
      return opcode;
    }
}

static const char *
emit_mlt (char prefix, char opcode, const char * args)
{
  expressionS arg;
  const char *p;
  char *q;

  p = parse_exp (args, & arg);
  
  if (arg.X_md != 0 || arg.X_op != O_register || !(arg.X_add_number & R_ARITH))
    ill_op ();

  q = frag_more (2);
  
  if (ins_ok & INS_Z80N)
    {
      if (arg.X_add_number != REG_DE)
        ill_op ();
      q[0] = 0xED;
      q[1] = 0x30;
    }
  else
    {
      q[0] = prefix;
      q[1] = opcode | ((arg.X_add_number & 3) << 4);
    }

  return p;
}

/* MUL D,E (Z80N only) */
static int is_valid_register(const expressionS *expr, int expected_reg)
{
  return expr->X_md == 0 && 
         expr->X_op == O_register && 
         expr->X_add_number == expected_reg;
}

static const char *parse_register_pair(const char *args, expressionS *r1, expressionS *r2)
{
  const char *p = parse_exp(args, r1);
  
  if (*p++ != ',')
    error(_("bad instruction syntax"));
  
  return parse_exp(p, r2);
}

static void validate_de_register_pair(const expressionS *r1, const expressionS *r2)
{
  if (!is_valid_register(r1, REG_D) || !is_valid_register(r2, REG_E))
    ill_op();
}

static void emit_instruction_bytes(char prefix, char opcode)
{
  char *q = frag_more(2);
  *q++ = prefix;
  *q = opcode;
}

static const char *
emit_mul(char prefix, char opcode, const char *args)
{
  expressionS r1, r2;
  
  const char *p = parse_register_pair(args, &r1, &r2);
  validate_de_register_pair(&r1, &r2);
  emit_instruction_bytes(prefix, opcode);
  
  return p;
}

static int is_invalid_expression(const expressionS *expr)
{
  return expr->X_md != 0 || expr->X_op == O_register || expr->X_op == O_md1;
}

static int is_invalid_nn_expression(const expressionS *nn)
{
  return nn->X_md != 0 || nn->X_op == O_md1;
}

static const char *parse_register_expression(const char *args, expressionS *expr)
{
  const char *p = parse_exp(args, expr);
  if (*p++ != ',')
    error(_("bad instruction syntax"));
  return p;
}

static void emit_nn_operand(const expressionS *nn, char *q)
{
  if (nn->X_op == O_register && nn->X_add_number == REG_A)
    {
      *q = 0x92;
    }
  else if (nn->X_op != O_register)
    {
      *q = 0x91;
      emit_byte(nn, BFD_RELOC_8);
    }
  else
    {
      ill_op();
    }
}

static const char *
emit_nextreg(char prefix, char opcode ATTRIBUTE_UNUSED, const char *args)
{
  expressionS rr, nn;
  const char *p;
  char *q;

  p = parse_register_expression(args, &rr);
  p = parse_exp(p, &nn);
  
  if (is_invalid_expression(&rr) || is_invalid_nn_expression(&nn))
    ill_op();
    
  q = frag_more(2);
  *q++ = prefix;
  emit_byte(&rr, BFD_RELOC_8);
  emit_nn_operand(&nn, q);
  
  return p;
}

static const char *
emit_pea (char prefix, char opcode, const char * args)
{
  expressionS arg;
  const char *p;
  
  p = parse_lea_pea_args (args, & arg);
  validate_pea_arg(&arg);
  emit_pea_instruction(prefix, opcode, &arg);
  emit_pea_displacement(&arg);
  
  return p;
}

static void validate_pea_arg(const expressionS *arg)
{
  if (arg->X_md != 0
      || arg->X_op != O_add
      || !(arg->X_add_number & R_INDEX))
    ill_op ();
}

static void emit_pea_instruction(char prefix, char opcode, const expressionS *arg)
{
  char *q = frag_more (2);
  *q++ = prefix;
  *q = opcode + (arg->X_add_number == REG_IY ? 1 : 0);
}

static void emit_pea_displacement(expressionS *arg)
{
  arg->X_op = O_symbol;
  arg->X_add_number = 0;
  emit_byte (arg, BFD_RELOC_Z80_DISP8);
}

static const char *
emit_reti (char prefix, char opcode, const char * args)
{
  const char GBZ80_PREFIX = 0x00;
  const char GBZ80_OPCODE = 0xD9;
  
  if (ins_ok & INS_GBZ80)
    return emit_insn (GBZ80_PREFIX, GBZ80_OPCODE, args);

  return emit_insn (prefix, opcode, args);
}

static const char *
parse_optional_a_register(const char *p, expressionS *arg_s)
{
  if (*p == ',' && arg_s->X_md == 0 && arg_s->X_op == O_register && arg_s->X_add_number == REG_A)
    {
      if (!(ins_ok & INS_EZ80))
        ill_op ();
      ++p;
      p = parse_exp (p, arg_s);
    }
  return p;
}

static void
emit_register_tst(char prefix, char opcode, int rnum, int is_md)
{
  char *q;
  
  if (is_md)
    {
      if (rnum != REG_HL)
        ill_op ();
      else
        rnum = 6;
    }
  
  q = frag_more (2);
  *q++ = prefix;
  *q = opcode | (rnum << 3);
}

static void
emit_immediate_tst(char prefix, char opcode, expressionS *arg_s)
{
  char *q;
  
  if (arg_s->X_md)
    ill_op ();
    
  q = frag_more (2);
  if (ins_ok & INS_Z80N)
    {
      *q++ = 0xED;
      *q = 0x27;
    }
  else
    {
      *q++ = prefix;
      *q = opcode | 0x60;
    }
  emit_byte (arg_s, BFD_RELOC_8);
}

static const char *
emit_tst (char prefix, char opcode, const char *args)
{
  expressionS arg_s;
  const char *p;

  p = parse_exp (args, & arg_s);
  p = parse_optional_a_register(p, & arg_s);

  switch (arg_s.X_op)
    {
    case O_md1:
      ill_op ();
      break;
    case O_register:
      emit_register_tst(prefix, opcode, arg_s.X_add_number, arg_s.X_md);
      break;
    default:
      emit_immediate_tst(prefix, opcode, & arg_s);
    }
  return p;
}

static const char *
emit_insn_n (char prefix, char opcode, const char *args)
{
  expressionS arg;
  const char *p;
  char *q;

  p = parse_exp (args, & arg);
  
  if (arg.X_md || arg.X_op == O_register || arg.X_op == O_md1)
    ill_op ();

  q = frag_more (2);
  *q++ = prefix;
  *q = opcode;
  
  emit_byte (& arg, BFD_RELOC_8);

  return p;
}

static int is_quote_char(char c)
{
    return c == '\"' || c == '\'';
}

static const char* process_string_literal(const char* p)
{
    char quote = *p;
    const char* q = ++p;
    int cnt = 0;
    
    while (*p && quote != *p)
    {
        ++p;
        ++cnt;
    }
    
    char* u = frag_more(cnt);
    memcpy(u, q, cnt);
    
    if (!*p)
        as_warn(_("unterminated string"));
    else
        p = skip_space(p + 1);
    
    return p;
}

static const char* process_expression(const char* p)
{
    expressionS exp;
    p = parse_exp(p, &exp);
    
    if (exp.X_op == O_md1 || exp.X_op == O_register)
    {
        ill_op();
        return NULL;
    }
    
    if (exp.X_md)
        as_warn(_("parentheses ignored"));
    
    emit_byte(&exp, BFD_RELOC_8);
    return skip_space(p);
}

static void emit_data(int size ATTRIBUTE_UNUSED)
{
    if (is_it_end_of_statement())
    {
        demand_empty_rest_of_line();
        return;
    }
    
    const char* p = skip_space(input_line_pointer);
    
    do
    {
        if (is_quote_char(*p))
        {
            p = process_string_literal(p);
        }
        else
        {
            p = process_expression(p);
            if (!p)
                break;
        }
    }
    while (*p++ == ',');
    
    input_line_pointer = (char*)(p - 1);
}

static void
z80_cons (int size)
{
  const char *p;

  if (is_it_end_of_statement ())
    {
      demand_empty_rest_of_line ();
      return;
    }
  
  p = skip_space (input_line_pointer);
  process_expression_list (p, size);
}

static void
process_expression_list (const char *p, int size)
{
  expressionS exp;
  
  do
    {
      p = parse_exp (p, &exp);
      
      if (!validate_expression (&exp))
        break;
      
      emit_expression (&exp, size);
      p = skip_space (p);
    } 
  while (*p++ == ',');
  
  input_line_pointer = (char *)(p - 1);
}

static int
validate_expression (expressionS *exp)
{
  if (exp->X_op == O_md1 || exp->X_op == O_register)
    {
      ill_op ();
      return 0;
    }
  
  if (exp->X_md)
    as_warn (_("parentheses ignored"));
  
  return 1;
}

static void
emit_expression (expressionS *exp, int size)
{
  emit_data_val (exp, size);
}

/* next functions were commented out because it is difficult to mix
   both ADL and Z80 mode instructions within one COFF file:
   objdump cannot recognize point of mode switching.
*/
static void
set_cpu_mode (int mode)
{
  if (ins_ok & INS_EZ80)
    cpu_mode = mode;
  else
    error (_("CPU mode is unsupported by target"));
}

static void
assume (int arg ATTRIBUTE_UNUSED)
{
  char *name;
  char c;
  int n;

  input_line_pointer = (char*)skip_space (input_line_pointer);
  c = get_symbol_name (& name);
  if (strncasecmp (name, "ADL", 4) != 0)
    {
      ill_op ();
      return;
    }

  restore_line_pointer (c);
  input_line_pointer = (char*)skip_space (input_line_pointer);
  if (*input_line_pointer++ != '=')
    {
      error (_("assignment expected"));
      return;
    }
  input_line_pointer = (char*)skip_space (input_line_pointer);
  n = get_single_number ();

  set_cpu_mode (n);
}

static const char *
emit_mulub (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  const char *p = skip_space (args);
  
  if (TOLOWER (*p++) != 'a' || *p++ != ',')
    {
      ill_op ();
      return p;
    }
  
  char reg = TOLOWER (*p++);
  
  if (reg < 'b' || reg > 'e')
    {
      ill_op ();
      return p;
    }
  
  check_mach (INS_R800);
  
  if (*skip_space (p))
    {
      ill_op ();
      return p;
    }
  
  char *q = frag_more (2);
  *q++ = prefix;
  *q = opcode + ((reg - 'b') << 3);
  
  return p;
}

static const char *
emit_muluw (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  const char *p = skip_space (args);
  
  if (TOLOWER (*p++) != 'h' || TOLOWER (*p++) != 'l' || *p++ != ',')
    {
      ill_op ();
      return p;
    }
  
  expressionS reg;
  p = parse_exp (p, &reg);
  
  if (reg.X_md || reg.X_op != O_register)
    return p;
    
  if (reg.X_add_number != REG_BC && reg.X_add_number != REG_SP)
    {
      ill_op ();
      return p;
    }
  
  check_mach (INS_R800);
  char *q = frag_more (2);
  *q++ = prefix;
  *q = opcode + ((reg.X_add_number & 3) << 4);
  
  return p;
}

static int parse_suffix(const char **suffix, char *sbuf) {
    const char *p = *suffix;
    int i;
    
    if (*p++ != '.')
        return 0;
    
    for (i = 0; (i < 3) && (ISALPHA(*p)); i++)
        sbuf[i] = TOLOWER(*p++);
    
    if (*p && !is_whitespace(*p))
        return 0;
    
    *suffix = p;
    sbuf[i] = 0;
    return 1;
}

static int get_suffix_index(const char *sbuf) {
    static const char sf[8][4] = {
        "il", "is", "l", "lil", "lis", "s", "sil", "sis"
    };
    
    const char (*t)[4] = bsearch(sbuf, sf, ARRAY_SIZE(sf), sizeof(sf[0]), 
                                 (int(*)(const void*, const void*))strcmp);
    if (t == NULL)
        return -1;
    
    return t - sf;
}

static int get_instruction_value(int index) {
    static const int values_cpu_mode_true[8] = {
        0x5B, 0x49, 0x5B, 0x5B, 0x49, 0x52, 0x52, 0x40
    };
    static const int values_cpu_mode_false[8] = {
        0x52, 0x40, 0x49, 0x5B, 0x49, 0x40, 0x52, 0x40
    };
    
    return cpu_mode ? values_cpu_mode_true[index] : values_cpu_mode_false[index];
}

static void set_instruction_mode(int value) {
    #define MODE_SIS 0x40
    #define MODE_LIS 0x49
    #define MODE_SIL 0x52
    #define MODE_LIL 0x5B
    
    switch (value) {
    case MODE_SIS:
        inst_mode = INST_MODE_FORCED | INST_MODE_S | INST_MODE_IS;
        break;
    case MODE_LIS:
        inst_mode = INST_MODE_FORCED | INST_MODE_L | INST_MODE_IS;
        break;
    case MODE_SIL:
        inst_mode = INST_MODE_FORCED | INST_MODE_S | INST_MODE_IL;
        break;
    case MODE_LIL:
        inst_mode = INST_MODE_FORCED | INST_MODE_L | INST_MODE_IL;
        break;
    }
}

static int assemble_suffix(const char **suffix) {
    char sbuf[4];
    int index;
    int value;
    
    if (!parse_suffix(suffix, sbuf))
        return 0;
    
    index = get_suffix_index(sbuf);
    if (index < 0)
        return 0;
    
    value = get_instruction_value(index);
    *frag_more(1) = value;
    set_instruction_mode(value);
    
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
  int old_ins;

  if (!sdcc_compat)
    as_fatal (_("Invalid directive"));

  old_ins = ins_ok;
  ins_ok = (ins_ok & INS_MARCH_MASK) | inss;
  if (old_ins != ins_ok)
    cpu_mode = 0;
}

static void
ignore (int arg ATTRIBUTE_UNUSED)
{
  ignore_rest_of_line ();
}

static void
area (int arg)
{
  char *p;
  if (!sdcc_compat)
    as_fatal (_("Invalid directive"));
  
  p = find_delimiter();
  
  if (*p == '(')
    {
      process_area_with_parenthesis(p, arg);
    }
  else
    {
      psect (arg);
    }
}

static char*
find_delimiter (void)
{
  char *p;
  for (p = input_line_pointer; *p && *p != '(' && *p != '\n'; p++)
    ;
  return p;
}

static void
process_area_with_parenthesis (char *p, int arg)
{
  *p = '\n';
  psect (arg);
  *p++ = '(';
  ignore_rest_of_line ();
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
  char * old_ptr;
  table_t *insp;

  err_flag = 0;
  inst_mode = cpu_mode ? (INST_MODE_L | INST_MODE_IL) : (INST_MODE_S | INST_MODE_IS);
  old_ptr = input_line_pointer;
  p = skip_space (str);
  
  int opcode_len = extract_opcode(p);
  
  if (opcode_len == BUFLEN)
    {
      handle_opcode_too_long();
      input_line_pointer = old_ptr;
      return;
    }

  dwarf2_emit_insn (0);
  
  if (!validate_syntax(&p))
    {
      input_line_pointer = old_ptr;
      return;
    }
  
  buf[opcode_len] = 0;
  p = skip_space (p);
  key = buf;

  insp = bsearch (&key, instab, ARRAY_SIZE (instab),
                sizeof (instab[0]), key_cmp);
  
  if (!is_instruction_valid(insp))
    {
      *frag_more (1) = 0;
      as_bad (_("Unknown instruction `%s'"), buf);
    }
  else
    {
      p = process_instruction(insp, p);
    }
  
  input_line_pointer = old_ptr;
}

static int extract_opcode(const char *p)
{
  int i;
  for (i = 0; (i < BUFLEN) && (ISALPHA (*p) || ISDIGIT (*p)); i++)
    buf[i] = TOLOWER (*p++);
  return i;
}

static void handle_opcode_too_long(void)
{
  buf[BUFLEN-3] = buf[BUFLEN-2] = '.';
  buf[BUFLEN-1] = 0;
  as_bad (_("Unknown instruction '%s'"), buf);
}

static int validate_syntax(const char **p)
{
  if ((**p) && !is_whitespace (**p))
    {
      if (**p != '.' || !(ins_ok & INS_EZ80) || !assemble_suffix (p))
        {
          as_bad (_("syntax error"));
          return 0;
        }
    }
  return 1;
}

static int is_instruction_valid(table_t *insp)
{
  return insp && (!insp->inss || (insp->inss & ins_ok));
}

static const char* process_instruction(table_t *insp, const char *p)
{
  p = insp->fp (insp->prefix, insp->opcode, p);
  p = skip_space (p);
  if ((!err_flag) && *p)
    as_bad (_("junk at end of line, "
              "first unrecognized character is `%c'"), *p);
  return p;
}

static int
signed_overflow (signed long value, unsigned bitsize)
{
  signed long max = (signed long) ((1UL << (bitsize - 1)) - 1);
  signed long min = -max - 1;
  return value < min || value > max;
}

static int unsigned_overflow(unsigned long value, unsigned bitsize)
{
    const unsigned HALF_SHIFT = 1;
    unsigned remaining_bits = bitsize - HALF_SHIFT;
    unsigned long shifted_value = value >> remaining_bits >> HALF_SHIFT;
    return shifted_value != 0;
}

static int
is_overflow (long value, unsigned bitsize)
{
  return value < 0 ? signed_overflow (value, bitsize) : unsigned_overflow (value, bitsize);
}

void
md_apply_fix (fixS * fixP, valueT* valP, segT seg)
{
  long val = *valP;
  char *p_lit = fixP->fx_where + fixP->fx_frag->fr_literal;

  if (fixP->fx_addsy == NULL)
    fixP->fx_done = 1;
  else if (fixP->fx_pcrel)
    {
      segT s = S_GET_SEGMENT (fixP->fx_addsy);
      if (s == seg || s == absolute_section)
	{
	  val += S_GET_VALUE (fixP->fx_addsy);
	  fixP->fx_done = 1;
	}
    }

  set_overflow_flag(fixP);
  apply_relocation(fixP, p_lit, val);
}

static void
set_overflow_flag(fixS *fixP)
{
  switch (fixP->fx_r_type)
    {
    case BFD_RELOC_8_PCREL:
    case BFD_RELOC_Z80_DISP8:
    case BFD_RELOC_8:
    case BFD_RELOC_16:
    case BFD_RELOC_24:
    case BFD_RELOC_32:
    case BFD_RELOC_Z80_16_BE:
      fixP->fx_no_overflow = 0;
      break;
    default:
      fixP->fx_no_overflow = 1;
      break;
    }
}

static void
write_byte(char **p_lit, long val)
{
  *(*p_lit)++ = val;
}

static void
write_word_le(char **p_lit, long val)
{
  write_byte(p_lit, val);
  write_byte(p_lit, val >> 8);
}

static void
write_word_be(char **p_lit, long val)
{
  write_byte(p_lit, val >> 8);
  write_byte(p_lit, val);
}

static void
write_24bit_le(char **p_lit, long val)
{
  write_word_le(p_lit, val);
  write_byte(p_lit, val >> 16);
}

static void
write_32bit_le(char **p_lit, long val)
{
  write_word_le(p_lit, val);
  write_word_le(p_lit, val >> 16);
}

static void
check_overflow_and_warn(fixS *fixP, long val, int bits)
{
  if (fixP->fx_done && is_overflow(val, bits))
    as_warn_where (fixP->fx_file, fixP->fx_line,
                   _("%d-bit overflow (%+ld)"), bits, val);
}

static void
check_signed_overflow(fixS *fixP, long val)
{
  if (fixP->fx_done && signed_overflow (val, 8))
    as_bad_where (fixP->fx_file, fixP->fx_line,
                  _("8-bit signed offset out of range (%+ld)"), val);
}

static void
apply_relocation(fixS *fixP, char *p_lit, long val)
{
  switch (fixP->fx_r_type)
    {
    case BFD_RELOC_8_PCREL:
    case BFD_RELOC_Z80_DISP8:
      check_signed_overflow(fixP, val);
      write_byte(&p_lit, val);
      break;

    case BFD_RELOC_Z80_BYTE0:
      write_byte(&p_lit, val);
      break;

    case BFD_RELOC_Z80_BYTE1:
      write_byte(&p_lit, val >> 8);
      break;

    case BFD_RELOC_Z80_BYTE2:
      write_byte(&p_lit, val >> 16);
      break;

    case BFD_RELOC_Z80_BYTE3:
      write_byte(&p_lit, val >> 24);
      break;

    case BFD_RELOC_8:
      check_overflow_and_warn(fixP, val, 8);
      write_byte(&p_lit, val);
      break;

    case BFD_RELOC_Z80_WORD1:
      write_word_le(&p_lit, val >> 16);
      break;

    case BFD_RELOC_Z80_WORD0:
      write_word_le(&p_lit, val);
      break;

    case BFD_RELOC_16:
      check_overflow_and_warn(fixP, val, 16);
      write_word_le(&p_lit, val);
      break;

    case BFD_RELOC_24:
      check_overflow_and_warn(fixP, val, 24);
      write_24bit_le(&p_lit, val);
      break;

    case BFD_RELOC_32:
      check_overflow_and_warn(fixP, val, 32);
      write_32bit_le(&p_lit, val);
      break;

    case BFD_RELOC_Z80_16_BE:
      write_word_be(&p_lit, val);
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
  reloc->sym_ptr_ptr = notes_alloc (sizeof (asymbol *));
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

int z80_tc_labels_without_colon(void)
{
  return colonless_labels;
}

int
z80_tc_label_is_local (const char *name)
{
  if (local_label_prefix == NULL)
    return 0;
  
  const char *prefix = local_label_prefix;
  const char *current = name;
  
  while (*prefix && *current && *current == *prefix)
  {
    prefix++;
    current++;
  }
  
  return *prefix == '\0';
}

/* Parse floating point number from string and compute mantissa and
   exponent. Mantissa is normalized.
*/
#define EXP_MIN -0x10000
#define EXP_MAX 0x10000
static int parse_sign(char **p, bool *signP)
{
    *signP = (**p == '-');
    if (*signP || **p == '+')
        ++(*p);
    return 1;
}

static int parse_special_values(char *p, uint64_t *mantissaP, int *expP)
{
    if (strncasecmp(p, "NaN", 3) == 0)
    {
        *mantissaP = 0;
        *expP = 0;
        input_line_pointer = p + 3;
        return 1;
    }
    if (strncasecmp(p, "inf", 3) == 0)
    {
        *mantissaP = 1ull << 63;
        *expP = EXP_MAX;
        input_line_pointer = p + 3;
        return 1;
    }
    return 0;
}

static void process_digits(char **p, uint64_t *mantissa, int *exponent, int *decrement_exp)
{
    #define MANTISSA_OVERFLOW_THRESHOLD 60
    #define ROUNDING_THRESHOLD '5'
    
    for (; ISDIGIT(**p); ++(*p))
    {
        if (*decrement_exp)
            --(*exponent);
            
        if (*mantissa >> MANTISSA_OVERFLOW_THRESHOLD)
        {
            if (**p >= ROUNDING_THRESHOLD)
                (*mantissa)++;
            break;
        }
        *mantissa = *mantissa * 10 + (**p - '0');
    }
}

static void skip_remaining_digits(char **p, int *exponent, int increment)
{
    for (; ISDIGIT(**p); ++(*p))
        if (increment)
            (*exponent)++;
}

static void parse_decimal_part(char **p, uint64_t *mantissa, int *exponent)
{
    if (**p != '.')
        return;
        
    (*p)++;
    if (!*exponent)
    {
        int decrement = 1;
        process_digits(p, mantissa, exponent, &decrement);
    }
    skip_remaining_digits(p, exponent, 0);
}

static void parse_exponent_part(char **p, int *exponent)
{
    #define MAX_EXPONENT_VALUE 100
    
    if (**p != 'e' && **p != 'E')
        return;
        
    int es, t = 0;
    ++(*p);
    es = (**p == '-');
    if (es || **p == '+')
        (*p)++;
        
    for (; ISDIGIT(**p); ++(*p))
    {
        if (t < MAX_EXPONENT_VALUE)
            t = t * 10 + (**p - '0');
    }
    *exponent += es ? -t : t;
}

static void normalize_mantissa(uint64_t *mantissa, int *exponent)
{
    for (; *mantissa <= ~0ull/10; --(*exponent))
        *mantissa *= 10;
}

static void convert_to_binary_exponent(uint64_t *mantissa, int *exponent, int *binary_exp)
{
    #define BASE_BINARY_EXPONENT 64
    
    *binary_exp = BASE_BINARY_EXPONENT;
    
    for (; *exponent > 0; --(*exponent))
    {
        while (*mantissa > ~0ull/10)
        {
            *mantissa >>= 1;
            (*binary_exp)++;
        }
        *mantissa *= 10;
    }
    
    for (; *exponent < 0; ++(*exponent))
    {
        while (!(*mantissa >> 63))
        {
            *mantissa <<= 1;
            (*binary_exp)--;
        }
        *mantissa /= 10;
    }
    
    for (; !(*mantissa >> 63); --(*binary_exp))
        *mantissa <<= 1;
}

static int handle_zero_mantissa(uint64_t mantissa, uint64_t *mantissaP, int *expP)
{
    if (mantissa == 0)
    {
        *mantissaP = 1ull << 63;
        *expP = EXP_MIN;
        return 1;
    }
    return 0;
}

static int str_to_broken_float(bool *signP, uint64_t *mantissaP, int *expP)
{
    char *p = (char*)skip_space(input_line_pointer);
    uint64_t mantissa = 0;
    int exponent = 0;
    
    parse_sign(&p, signP);
    
    if (parse_special_values(p, mantissaP, expP))
        return 1;
    
    int no_decrement = 0;
    process_digits(&p, &mantissa, &exponent, &no_decrement);
    skip_remaining_digits(&p, &exponent, 1);
    
    parse_decimal_part(&p, &mantissa, &exponent);
    parse_exponent_part(&p, &exponent);
    
    if (ISALNUM(*p) || *p == '.')
        return 0;
        
    input_line_pointer = p;
    
    if (handle_zero_mantissa(mantissa, mantissaP, expP))
        return 1;
    
    normalize_mantissa(&mantissa, &exponent);
    
    int binary_exp;
    convert_to_binary_exponent(&mantissa, &exponent, &binary_exp);
    
    *mantissaP = mantissa;
    *expP = binary_exp;
    return 1;
}

static const char *
str_to_zeda32(char *litP, int *sizeP)
{
  uint64_t mantissa;
  bool sign;
  int exponent;

  *sizeP = 4;
  if (!str_to_broken_float(&sign, &mantissa, &exponent))
    return _("invalid syntax");

  exponent--;
  mantissa = process_mantissa(mantissa);
  
  if (check_mantissa_overflow(mantissa))
  {
    mantissa >>= 1;
    exponent++;
  }

  handle_exponent_bounds(&exponent, &mantissa, sign);
  adjust_final_mantissa(&mantissa, sign);
  
  write_mantissa_bytes(litP, mantissa);
  litP[3] = 0x80 + exponent;
  
  return NULL;
}

static uint64_t process_mantissa(uint64_t mantissa)
{
  #define MANTISSA_SHIFT_BITS 39
  #define ROUNDING_INCREMENT 1
  #define FINAL_SHIFT_BITS 1
  
  mantissa >>= MANTISSA_SHIFT_BITS;
  mantissa += ROUNDING_INCREMENT;
  mantissa >>= FINAL_SHIFT_BITS;
  
  return mantissa;
}

static bool check_mantissa_overflow(uint64_t mantissa)
{
  #define MANTISSA_OVERFLOW_BIT 24
  return (mantissa >> MANTISSA_OVERFLOW_BIT) != 0;
}

static void handle_exponent_bounds(int *exponent, uint64_t *mantissa, bool sign)
{
  #define MIN_EXPONENT -127
  #define MAX_EXPONENT 127
  #define ZERO_EXPONENT -128
  #define NEGATIVE_OVERFLOW_MANTISSA 0xc00000
  #define POSITIVE_OVERFLOW_MANTISSA 0x400000
  #define ZERO_MANTISSA_VALUE 0x200000
  
  if (*exponent < MIN_EXPONENT)
  {
    *exponent = ZERO_EXPONENT;
    *mantissa = 0;
  }
  else if (*exponent > MAX_EXPONENT)
  {
    *exponent = ZERO_EXPONENT;
    *mantissa = sign ? NEGATIVE_OVERFLOW_MANTISSA : POSITIVE_OVERFLOW_MANTISSA;
  }
  else if (*mantissa == 0)
  {
    *exponent = ZERO_EXPONENT;
    *mantissa = ZERO_MANTISSA_VALUE;
  }
}

static void adjust_final_mantissa(uint64_t *mantissa, bool sign)
{
  #define MANTISSA_23_BIT_MASK ((1ull << 23) - 1)
  
  if (!sign && *mantissa != 0)
  {
    *mantissa &= MANTISSA_23_BIT_MASK;
  }
}

static void write_mantissa_bytes(char *litP, uint64_t mantissa)
{
  #define BYTE_SIZE 8
  #define MANTISSA_BYTES 3
  
  for (unsigned i = 0; i < MANTISSA_BYTES * BYTE_SIZE; i += BYTE_SIZE)
  {
    *litP++ = mantissa >> i;
  }
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
static const char *
str_to_float48(char *litP, int *sizeP)
{
  const int FLOAT48_SIZE = 6;
  const int MANTISSA_INITIAL_SHIFT = 23;
  const int MANTISSA_BITS = 40;
  const int EXPONENT_MIN = -127;
  const int EXPONENT_MAX = 127;
  const int EXPONENT_BIAS = 0x80;
  const uint64_t MANTISSA_SIGN_MASK = (1ull << 39) - 1;
  
  uint64_t mantissa;
  bool sign;
  int exponent;

  *sizeP = FLOAT48_SIZE;
  
  if (!str_to_broken_float(&sign, &mantissa, &exponent))
    return _("invalid syntax");
  
  mantissa = process_mantissa(mantissa);
  
  if (mantissa >> MANTISSA_BITS)
    {
      mantissa >>= 1;
      ++exponent;
    }
  
  if (exponent < EXPONENT_MIN)
    {
      memset(litP, 0, FLOAT48_SIZE);
      return NULL;
    }
  
  if (exponent > EXPONENT_MAX)
    return _("overflow");
  
  if (!sign)
    mantissa &= MANTISSA_SIGN_MASK;
  
  write_float48_bytes(litP, exponent + EXPONENT_BIAS, mantissa);
  
  return NULL;
}

static uint64_t process_mantissa(uint64_t mantissa)
{
  mantissa >>= MANTISSA_INITIAL_SHIFT;
  ++mantissa;
  mantissa >>= 1;
  return mantissa;
}

static void write_float48_bytes(char *litP, int biased_exponent, uint64_t mantissa)
{
  const int BYTE_SIZE = 8;
  const int MANTISSA_BYTES = 5;
  
  *litP++ = biased_exponent;
  
  for (int i = 0; i < MANTISSA_BYTES; i++)
    {
      *litP++ = mantissa & 0xFF;
      mantissa >>= BYTE_SIZE;
    }
}

static const char *
str_to_ieee754_h(char *litP, int *sizeP)
{
  return ieee_md_atof ('h', litP, sizeP, false);
}

static const char *
str_to_ieee754_s(char *litP, int *sizeP)
{
  return ieee_md_atof ('s', litP, sizeP, false);
}

static const char *
str_to_ieee754_d(char *litP, int *sizeP)
{
  return ieee_md_atof ('d', litP, sizeP, false);
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
  const int EZ80_ADL_ADDR_SIZE = 3;
  const int DEFAULT_ADDR_SIZE = 2;
  
  if (bfd_get_mach (abfd) == bfd_mach_ez80_adl)
    return EZ80_ADL_ADDR_SIZE;
  
  return DEFAULT_ADDR_SIZE;
}
