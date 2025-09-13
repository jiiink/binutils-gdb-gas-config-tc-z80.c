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
  if (!name || !ok || !err || !mode)
    as_fatal (_("Invalid parameters"));

  size_t len = strcspn (name, "+-");
  int found = 0;
  
  for (unsigned i = 0; i < ARRAY_SIZE (match_cpu_table); ++i)
    {
      if (strlen (match_cpu_table[i].name) == len &&
          !strncasecmp (name, match_cpu_table[i].name, len))
        {
          *ok = match_cpu_table[i].ins_ok;
          *err = match_cpu_table[i].ins_err;
          *mode = match_cpu_table[i].cpu_mode;
          found = 1;
          break;
        }
    }

  if (!found)
    as_fatal (_("Invalid CPU is specified: %s"), name);

  const char *ext_start = name + len;
  while (*ext_start)
    {
      char op = *ext_start;
      ext_start++;
      len = strcspn (ext_start, "+-");
      found = 0;
      
      for (unsigned i = 0; i < ARRAY_SIZE (match_ext_table); ++i)
        {
          if (strlen (match_ext_table[i].name) == len &&
              !strncasecmp (ext_start, match_ext_table[i].name, len))
            {
              if (op == '+')
                {
                  *ok |= match_ext_table[i].ins_ok;
                  *err &= ~match_ext_table[i].ins_ok;
                  *mode |= match_ext_table[i].cpu_mode;
                }
              else
                {
                  *ok &= ~match_ext_table[i].ins_ok;
                  *err |= match_ext_table[i].ins_ok;
                  *mode &= ~match_ext_table[i].cpu_mode;
                }
              found = 1;
              break;
            }
        }
      
      if (!found)
        as_fatal (_("Invalid EXTENSION is specified: %s"), ext_start);
      
      ext_start += len;
    }
}

static int
setup_instruction (const char *inst, int *add, int *sub)
{
  typedef struct {
    const char *name;
    int value;
  } instruction_t;

  static const instruction_t instructions[] = {
    {"idx-reg-halves", INS_IDX_HALF},
    {"sli", INS_SLI},
    {"op-ii-ld", INS_ROT_II_LD},
    {"in-f-c", INS_IN_F_C},
    {"out-c-0", INS_OUT_C_0}
  };

  if (inst == NULL || add == NULL || sub == NULL) {
    return 0;
  }

  for (size_t i = 0; i < sizeof(instructions) / sizeof(instructions[0]); i++) {
    if (strcmp(inst, instructions[i].name) == 0) {
      *add |= instructions[i].value;
      *sub &= ~instructions[i].value;
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

  if (strcasecmp (arg, "math48") == 0)
    return str_to_float48;

  if (strcasecmp (arg, "half") == 0)
    return str_to_ieee754_h;

  if (strcasecmp (arg, "single") == 0)
    return str_to_ieee754_s;

  if (strcasecmp (arg, "double") == 0)
    return str_to_ieee754_d;

  as_fatal (_("invalid floating point numbers type `%s'"), arg);
  return NULL;
}

static int
setup_instruction_list (const char *list, int *add, int *sub)
{
  char buf[16];
  const char *current = list;
  const char *next;
  int count = 0;
  
  if (list == NULL || add == NULL || sub == NULL)
    return 0;
    
  while (*current != '\0')
    {
      next = strchr (current, ',');
      size_t len = (next != NULL) ? (size_t)(next - current) : strlen (current);
      
      if (len == 0 || len >= sizeof (buf))
        {
          as_bad (_("invalid INST in command line: %s"), current);
          return 0;
        }
        
      memcpy (buf, current, len);
      buf[len] = '\0';
      
      if (!setup_instruction (buf, add, sub))
        {
          as_bad (_("invalid INST in command line: %s"), buf);
          return 0;
        }
        
      count++;
      current = (next != NULL) ? next + 1 : current + len;
    }
    
  return count;
}

int
md_parse_option (int c, const char* arg)
{
  const char* march_arg = NULL;
  
  switch (c)
    {
    case OPTION_MARCH:
      march_arg = arg;
      break;
    case OPTION_MACH_Z80:
      march_arg = "z80";
      break;
    case OPTION_MACH_R800:
      march_arg = "r800";
      break;
    case OPTION_MACH_Z180:
      march_arg = "z180";
      break;
    case OPTION_MACH_EZ80_Z80:
      march_arg = "ez80";
      break;
    case OPTION_MACH_EZ80_ADL:
      march_arg = "ez80+adl";
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
    default:
      return 0;
    }

  if (march_arg != NULL)
    {
      setup_march (march_arg, & ins_ok, & ins_err, & cpu_mode);
    }

  return 1;
}

void
md_show_usage (FILE * f)
{
  unsigned i;
  
  fprintf (f, _("\nCPU model options:\n"));
  fprintf (f, _("  -march=CPU[+EXT...][-EXT...]\n"));
  fprintf (f, _("\t\t\t  generate code for CPU, where CPU is one of:\n"));
  
  for (i = 0; i < ARRAY_SIZE(match_cpu_table); ++i)
    fprintf (f, "  %-8s\t\t  %s\n", match_cpu_table[i].name, match_cpu_table[i].comment);
  
  fprintf (f, _("And EXT is combination (+EXT - add, -EXT - remove) of:\n"));
  
  for (i = 0; i < ARRAY_SIZE(match_ext_table); ++i)
    fprintf (f, "  %-8s\t\t  %s\n", match_ext_table[i].name, match_ext_table[i].comment);
  
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
  expressionS nul = {0};
  expressionS reg = {0};
  char *saved_input_ptr;
  char buf[BUFLEN];

  if (ins_ok & INS_EZ80)
    listing_lhs_width = 6;

  reg.X_op = O_register;
  reg.X_md = 0;
  reg.X_add_symbol = NULL;
  reg.X_op_symbol = NULL;

  for (unsigned int i = 0; i < ARRAY_SIZE(regtable); ++i)
    {
      if (regtable[i].isa && !(regtable[i].isa & ins_ok))
        continue;

      reg.X_add_number = regtable[i].number;
      size_t name_len = strlen(regtable[i].name);
      
      if (name_len >= BUFLEN - 1)
        continue;

      buf[name_len] = '\0';
      unsigned int max_combinations = 1U << name_len;

      for (unsigned int combination = 0; combination < max_combinations; ++combination)
        {
          for (size_t k = 0; k < name_len; ++k)
            {
              buf[k] = (combination & (1U << k)) 
                       ? TOUPPER(regtable[i].name[k]) 
                       : regtable[i].name[k];
            }
          
          symbolS *psym = symbol_find_or_make(buf);
          if (psym != NULL)
            {
              S_SET_SEGMENT(psym, reg_section);
              symbol_set_value_expression(psym, &reg);
            }
        }
    }

  saved_input_ptr = input_line_pointer;
  input_line_pointer = (char *) "0";
  nul.X_md = 0;
  expression(&nul);
  input_line_pointer = saved_input_ptr;
  zero = make_expr_symbol(&nul);
  linkrelax = 0;
}

void z80_md_finish(void)
{
    static const struct {
        unsigned int mask;
        int mach_type;
    } mach_map[] = {
        {INS_Z80, bfd_mach_z80},
        {INS_R800, bfd_mach_r800},
        {INS_Z180, bfd_mach_z180},
        {INS_GBZ80, bfd_mach_gbz80},
        {INS_Z80N, bfd_mach_z80n}
    };
    
    int mach_type = 0;
    unsigned int march = ins_ok & INS_MARCH_MASK;
    
    if (march == INS_EZ80) {
        mach_type = cpu_mode ? bfd_mach_ez80_adl : bfd_mach_ez80_z80;
    } else {
        for (size_t i = 0; i < sizeof(mach_map) / sizeof(mach_map[0]); i++) {
            if (march == mach_map[i].mask) {
                mach_type = mach_map[i].mach_type;
                break;
            }
        }
    }
    
    bfd_set_arch_mach(stdoutput, TARGET_ARCH, mach_type);
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
    if (s == NULL) {
        return NULL;
    }
    
    while (*s != '\0' && is_whitespace(*s)) {
        ++s;
    }
    
    return s;
}

/* A non-zero return-value causes a continue in the
   function read_a_source_file () in ../read.c.  */
int
z80_start_line_hook (void)
{
  char *p;
  char buf[4];

  p = input_line_pointer;
  while (*p && *p != '\n')
    {
      if (*p == '\'')
        {
          if (p[1] != 0 && p[1] != '\'' && p[2] == '\'')
            {
              snprintf (buf, 4, "%3d", (unsigned char)p[1]);
              *p++ = buf[0];
              *p++ = buf[1];
              *p++ = buf[2];
              continue;
            }
          if (!handle_quoted_string(p, '\''))
            return 1;
        }
      else if (*p == '"')
        {
          if (!handle_quoted_string(p, '"'))
            return 1;
        }
      else if (*p == '#' && sdcc_compat)
        {
          handle_sdcc_immediate(p);
        }
      ++p;
    }

  if (sdcc_compat && *input_line_pointer == '0')
    handle_dollar_labels();

  if (is_name_beginner (*input_line_pointer))
    return handle_label_assignment();

  return 0;
}

static int
handle_quoted_string(char *p, char quote)
{
  char *start = p++;
  while (*p && *p != '\n' && *p != quote)
    ++p;
  
  if (*p != quote)
    {
      as_bad (_("-- unterminated string"));
      ignore_rest_of_line ();
      return 0;
    }
  return 1;
}

static void
handle_sdcc_immediate(char *p)
{
  if (is_whitespace (p[1]) && *skip_space (p + 1) == '(')
    {
      *p++ = '0';
      *p = '+';
    }
  else
    {
      *p = (p[1] == '(') ? '+' : ' ';
    }
}

static void
handle_dollar_labels(void)
{
  char *p = input_line_pointer;
  char *dollar = NULL;

  while (*p >= '0' && *p <= '9')
    ++p;

  if (p[0] == '$' && p[1] == ':')
    {
      dollar = p;
      p = input_line_pointer;
      while (*p == '0' && p < dollar - 1)
        {
          *p = ' ';
          ++p;
        }
    }
}

static int
handle_label_assignment(void)
{
  char *name;
  char c;
  char *rest;
  char *line_start;
  int len;

  line_start = input_line_pointer;
  
  if (ignore_input ())
    return 0;
    
  c = get_symbol_name (&name);
  rest = input_line_pointer + 1;
  
  if (c == ':' && *rest == ':')
    {
      if (sdcc_compat)
        *rest = ' ';
      ++rest;
    }
    
  rest = (char*)skip_space (rest);
  len = get_assignment_type(rest);
  
  if (len == 0 || (len > 2 && ISALPHA (rest[len])))
    {
      (void) restore_line_pointer (c);
      input_line_pointer = line_start;
      return 0;
    }

  if (line_start[-1] == '\n')
    {
      bump_line_counters ();
      LISTING_NEWLINE ();
    }
    
  input_line_pointer = rest + len - 1;
  
  if (len == 1 || len == 4)
    equals (name, 1);
  else
    equals (name, 0);
    
  return 1;
}

static int
get_assignment_type(char *rest)
{
  if (*rest == '=')
    return (rest[1] == '=') ? 2 : 1;
    
  if (*rest == '.')
    ++rest;
    
  if (strncasecmp (rest, "EQU", 3) == 0)
    return 3;
    
  if (strncasecmp (rest, "DEFL", 4) == 0)
    return 4;
    
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
  if (litP == NULL || sizeP == NULL)
    return NULL;

  switch (type)
    {
    case 'f':
    case 'F':
    case 's':
    case 'S':
      if (str_to_float != NULL)
        return str_to_float (litP, sizeP);
      break;
    case 'd':
    case 'D':
    case 'r':
    case 'R':
      if (str_to_double != NULL)
        return str_to_double (litP, sizeP);
      break;
    default:
      break;
    }
  return ieee_md_atof (type, litP, sizeP, false);
}

valueT
md_section_align (segT seg ATTRIBUTE_UNUSED, valueT size)
{
  return size;
}

long
md_pcrel_from (fixS * fixp)
{
  if (fixp == NULL || fixp->fx_frag == NULL)
    {
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
static int key_cmp(const void *a, const void *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    
    const char *const *ptr_a = (const char *const *)a;
    const char *const *ptr_b = (const char *const *)b;
    
    if (*ptr_a == NULL && *ptr_b == NULL) {
        return 0;
    }
    if (*ptr_a == NULL) {
        return -1;
    }
    if (*ptr_b == NULL) {
        return 1;
    }
    
    return strcmp(*ptr_a, *ptr_b);
}

char buf[BUFLEN];
const char *key = buf;

/* Prevent an error on a line from also generating
   a "junk at end of line" error message.  */
static char err_flag;

static void
error (const char * message)
{
  if (err_flag || message == NULL)
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
  if ((ins_type & ins_err) != 0)
  {
    ill_op ();
  }
  else
  {
    as_warn (_("undocumented instruction"));
  }
}

static void check_mach(int ins_type)
{
    if ((ins_type & ins_ok) == 0) {
        wrong_mach(ins_type);
    }
}

/* Check whether an expression is indirect.  */
static int
is_indir (const char *s)
{
  if (!s)
    return 0;
  
  int indir = (*s == '(');
  int depth = 0;
  const char *p = s;

  while (*p && *p != ',')
    {
      if (*p == '"' || *p == '\'')
        {
          char quote = *p;
          p++;
          while (*p && *p != quote && *p != '\n')
            {
              if (*p == '\\' && p[1])
                p++;
              p++;
            }
          if (*p)
            p++;
        }
      else if (*p == '(')
        {
          depth++;
          p++;
        }
      else if (*p == ')')
        {
          depth--;
          if (depth < 0)
            error (_("mismatched parentheses"));
          if (depth == 0)
            {
              const char *next = skip_space (p + 1);
              if (*next && *next != ',')
                indir = 0;
            }
          p++;
        }
      else
        {
          p++;
        }
    }

  if (depth != 0)
    error (_("mismatched parentheses"));

  return indir;
}

/* Check whether a symbol involves a register.  */
static bool
contains_register (symbolS *sym)
{
  if (!sym)
    return false;

  expressionS *ex = symbol_get_value_expression (sym);
  if (!ex)
    return false;

  switch (ex->X_op)
    {
    case O_register:
      return true;

    case O_add:
    case O_subtract:
      if (ex->X_op_symbol && contains_register (ex->X_op_symbol))
        return true;
      if (ex->X_add_symbol && contains_register (ex->X_add_symbol))
        return true;
      break;

    case O_uminus:
    case O_symbol:
      if (ex->X_add_symbol && contains_register (ex->X_add_symbol))
        return true;
      break;

    default:
      break;
    }

  return false;
}

/* Parse general expression, not looking for indexed addressing.  */
static const char *
parse_exp_not_indexed (const char *s, expressionS *op)
{
  const char *p;
  int indir;
  int make_shift = -1;

  memset (op, 0, sizeof (*op));
  p = skip_space (s);
  
  if (sdcc_compat && (*p == '<' || *p == '>'))
    {
      make_shift = (*p == '<') ? 0 : (cpu_mode ? 16 : 8);
      s = ++p;
      p = skip_space (p);
    }

  indir = (make_shift == -1) ? is_indir (p) : 0;
  op->X_md = indir;
  
  if (indir && (ins_ok & INS_GBZ80))
    {
      p = skip_space (p + 1);
      if (!strncasecmp (p, "hl", 2))
        {
          p = skip_space (p + 2);
          const char *next_char = skip_space (p + 1);
          if (*next_char == ')' && (*p == '+' || *p == '-'))
            {
              op->X_op = O_md1;
              op->X_add_symbol = NULL;
              op->X_add_number = (*p == '+') ? REG_HL : -REG_HL;
              input_line_pointer = (char*)(next_char + 1);
              return input_line_pointer;
            }
        }
    }
  
  input_line_pointer = (char*)s;
  expression (op);
  resolve_register (op);
  
  if (op->X_op == O_absent)
    {
      error (_("missing operand"));
    }
  else if (op->X_op == O_illegal)
    {
      error (_("bad expression syntax"));
    }

  if (make_shift >= 0)
    {
      expressionS data;
      memset (&data, 0, sizeof (data));
      data.X_op = O_constant;
      data.X_add_number = make_shift;
      
      op->X_add_symbol = make_expr_symbol (op);
      op->X_add_number = 0;
      op->X_op = O_right_shift;
      op->X_op_symbol = make_expr_symbol (&data);
    }
  
  return input_line_pointer;
}

static int
unify_indexed (expressionS *op)
{
  if (op == NULL || op->X_add_symbol == NULL)
    return 0;

  expressionS *sym_expr = symbol_get_value_expression(op->X_add_symbol);
  if (sym_expr == NULL || sym_expr->X_op != O_register)
    return 0;

  int rnum = sym_expr->X_add_number;
  if ((rnum != REG_IX && rnum != REG_IY) || contains_register(op->X_op_symbol))
    {
      ill_op();
      return 0;
    }

  if (op->X_op == O_subtract)
    {
      expressionS minus = {0};
      minus.X_op = O_uminus;
      minus.X_add_symbol = op->X_op_symbol;
      op->X_op_symbol = make_expr_symbol(&minus);
      op->X_op = O_add;
    }

  if (op->X_add_number != 0)
    {
      expressionS add = {0};
      add.X_op = O_symbol;
      add.X_add_number = op->X_add_number;
      add.X_add_symbol = op->X_op_symbol;
      op->X_add_symbol = make_expr_symbol(&add);
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
parse_exp (const char *s, expressionS *op)
{
  const char* res = parse_exp_not_indexed (s, op);
  
  if (!res || !op)
    return res;
    
  switch (op->X_op)
    {
    case O_add:
    case O_subtract:
      if (unify_indexed (op) && op->X_md)
        op->X_op = O_md1;
      break;
      
    case O_register:
      if (op->X_md && 
          (op->X_add_number == REG_IX || op->X_add_number == REG_IY))
        {
          op->X_add_symbol = zero;
          op->X_op = O_md1;
        }
      break;
      
    case O_constant:
      if (sdcc_compat && is_indir (res))
        {
          expressionS off = *op;
          res = parse_exp (res, op);
          
          if (!op || op->X_op != O_md1 || op->X_add_symbol != zero)
            {
              ill_op ();
            }
          else
            {
              op->X_add_symbol = make_expr_symbol (&off);
            }
        }
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
  if (!s || !op)
    return NULL;

  int i;
  for (i = 0; i < BUFLEN && ISALPHA(s[i]); ++i)
    {
      buf[i] = TOLOWER(s[i]);
    }

  if (i >= BUFLEN)
    return NULL;

  if (s[i] != 0 && s[i] != ',')
    return NULL;

  buf[i] = 0;
  
  struct reg_entry *cc_p = bsearch(&key, cc_tab, ARRAY_SIZE(cc_tab),
                                   sizeof(cc_tab[0]), key_cmp);
  
  if (!cc_p)
    return NULL;

  *op = cc_p->number;
  return s + i;
}

static const char *
emit_insn (char prefix, char opcode, const char * args)
{
  char *p;
  size_t insn_size = prefix ? 2 : 1;
  
  p = frag_more (insn_size);
  if (!p)
    return NULL;
    
  if (prefix)
    {
      *p++ = prefix;
    }
  *p = opcode;
  return args;
}

void z80_cons_fix_new (fragS *frag_p, int offset, int nbytes, expressionS *exp)
{
  static const bfd_reloc_code_real_type reloc_types[] =
    {
      BFD_RELOC_8,
      BFD_RELOC_16,
      BFD_RELOC_24,
      BFD_RELOC_32
    };

  if (frag_p == NULL || exp == NULL)
    {
      return;
    }

  if (nbytes < 1 || nbytes > 4)
    {
      as_bad (_("unsupported BFD relocation size %u"), nbytes);
      return;
    }

  fix_new_exp (frag_p, offset, nbytes, exp, 0, reloc_types[nbytes - 1]);
}

static void
emit_data_val (expressionS * val, int size)
{
  char *p;
  bfd_reloc_code_real_type r_type;

  p = frag_more (size);
  if (val->X_op == O_constant)
    {
      int i;

      if (! val->X_extrabit
	  && is_overflow (val->X_add_number, size * 8))
	as_warn ( _("%d-bit overflow (%+" PRId64 ")"), size * 8,
		  (int64_t) val->X_add_number);
      for (i = 0; i < size; ++i)
	p[i] = (val->X_add_number >> (i * 8)) & 0xff;
      return;
    }

  switch (size)
    {
    case 1: r_type = BFD_RELOC_8; break;
    case 2: r_type = BFD_RELOC_16; break;
    case 3: r_type = BFD_RELOC_24; break;
    case 4: r_type = BFD_RELOC_32; break;
    case 8: r_type = BFD_RELOC_64; break;
    default:
      as_fatal (_("invalid data size %d"), size);
    }

  if (   (val->X_op == O_register)
      || (val->X_op == O_md1)
      || contains_register (val->X_add_symbol)
      || contains_register (val->X_op_symbol))
    ill_op ();

  if (size <= 2 && val->X_op_symbol)
    {
      process_relocation_for_small_size(val, &size, &r_type, &p);
    }

  fix_new_exp (frag_now, p - frag_now->fr_literal, size, val, false, r_type);
}

static void
process_relocation_for_small_size(expressionS *val, int *size, bfd_reloc_code_real_type *r_type, char **p)
{
  bool simplify = true;
  int shift = symbol_get_value_expression (val->X_op_symbol)->X_add_number;
  
  if (val->X_op == O_bit_and && shift == (1 << (*size * 8)) - 1)
    shift = 0;
  else if (val->X_op != O_right_shift)
    shift = -1;

  if (*size == 1)
    {
      simplify = process_byte_relocation(shift, r_type);
    }
  else if (*size == 2)
    {
      simplify = process_word_relocation(shift, r_type, val, p, size);
    }

  if (simplify)
    {
      val->X_op = O_symbol;
      val->X_op_symbol = NULL;
      val->X_add_number = 0;
    }
}

static bool
process_byte_relocation(int shift, bfd_reloc_code_real_type *r_type)
{
  switch (shift)
    {
    case 0: *r_type = BFD_RELOC_Z80_BYTE0; return true;
    case 8: *r_type = BFD_RELOC_Z80_BYTE1; return true;
    case 16: *r_type = BFD_RELOC_Z80_BYTE2; return true;
    case 24: *r_type = BFD_RELOC_Z80_BYTE3; return true;
    default: return false;
    }
}

static bool
process_word_relocation(int shift, bfd_reloc_code_real_type *r_type, expressionS *val, char **p, int *size)
{
  switch (shift)
    {
    case 0: 
      *r_type = BFD_RELOC_Z80_WORD0; 
      return true;
    case 16: 
      *r_type = BFD_RELOC_Z80_WORD1; 
      return true;
    case 8:
      setup_split_word_fixup(val, p, r_type, BFD_RELOC_Z80_BYTE1, BFD_RELOC_Z80_BYTE2);
      *size = 1;
      return false;
    case 24:
      setup_split_word_fixup(val, p, r_type, 0, BFD_RELOC_Z80_BYTE3);
      *size = 1;
      return false;
    default: 
      return false;
    }
}

static void
setup_split_word_fixup(expressionS *val, char **p, bfd_reloc_code_real_type *r_type, 
                       bfd_reloc_code_real_type first_reloc, bfd_reloc_code_real_type second_reloc)
{
  val->X_op = O_symbol;
  val->X_op_symbol = NULL;
  val->X_add_number = 0;
  
  if (first_reloc != 0)
    {
      fix_new_exp (frag_now, (*p)++ - frag_now->fr_literal, 1, val, false, first_reloc);
    }
  *r_type = second_reloc;
}

static void
emit_byte (expressionS * val, bfd_reloc_code_real_type r_type)
{
  char *p;

  if (r_type == BFD_RELOC_8)
    {
      emit_data_val (val, 1);
      return;
    }

  p = frag_more (1);
  if (p == NULL)
    return;

  *p = val->X_add_number;

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

      if (val->X_add_number < -128 || val->X_add_number >= 128)
        {
          const char *error_msg = (r_type == BFD_RELOC_Z80_DISP8) 
                                   ? _("index overflow (%+" PRId64 ")")
                                   : _("offset overflow (%+" PRId64 ")");
          as_bad (error_msg, (int64_t) val->X_add_number);
        }
      return;
    }

  fix_new_exp (frag_now, p - frag_now->fr_literal, 1, val,
               r_type == BFD_RELOC_8_PCREL, r_type);
}

static void emit_word(expressionS *val)
{
  int data_size = (inst_mode & INST_MODE_IL) ? 3 : 2;
  emit_data_val(val, data_size);
}

static void
emit_mx (char prefix, char opcode, int shift, expressionS * arg)
{
  char *q;
  int rnum;

  if (!arg) {
    return;
  }

  rnum = arg->X_add_number;
  
  switch (arg->X_op)
    {
    case O_register:
      if (arg->X_md)
	{
	  if (rnum != REG_HL)
	    {
	      ill_op ();
	      return;
	    }
	  rnum = 6;
	}
      else
	{
	  if ((prefix == 0) && (rnum & R_INDEX))
	    {
	      prefix = (rnum & R_IX) ? 0xDD : 0xFD;
	      if (!(ins_ok & (INS_EZ80|INS_R800|INS_Z80N)))
                check_mach (INS_IDX_HALF);
	      rnum &= ~R_INDEX;
	    }
	  if (rnum > 7)
	    {
	      ill_op ();
	      return;
	    }
	}
      q = frag_more (prefix ? 2 : 1);
      if (prefix)
	*q++ = prefix;
      *q = opcode + (rnum << shift);
      break;
      
    case O_md1:
      if (ins_ok & INS_GBZ80)
        {
          ill_op ();
          return;
        }
      q = frag_more (2);
      *q++ = (rnum & R_IX) ? 0xDD : 0xFD;
      *q = prefix ? prefix : (opcode + (6 << shift));
      
      expressionS offset = *arg;
      offset.X_op = O_symbol;
      offset.X_add_number = 0;
      emit_byte (&offset, BFD_RELOC_Z80_DISP8);
      
      if (prefix)
	{
	  q = frag_more (1);
	  *q = opcode + (6 << shift);
	}
      break;
      
    default:
      abort ();
    }
}

/* The operand m may be r, (hl), (ix+d), (iy+d),
   if 0 = prefix m may also be ixl, ixh, iyl, iyh.  */
static const char *
emit_m (char prefix, char opcode, const char *args)
{
  expressionS arg_m;
  const char *p;

  if (args == NULL)
    {
      ill_op ();
      return NULL;
    }

  p = parse_exp (args, &arg_m);
  if (p == NULL)
    {
      ill_op ();
      return NULL;
    }

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

  if (arg_m.X_op == O_register)
    {
      emit_mx (prefix, opcode, 0, &arg_m);
      return p;
    }

  if (arg_m.X_op != O_md1)
    {
      ill_op ();
      return p;
    }

  if (*p != ',')
    {
      emit_mx (prefix, opcode, 0, &arg_m);
      return p;
    }

  p = parse_exp (p + 1, &arg_r);

  if (arg_r.X_md != 0 || arg_r.X_op != O_register || arg_r.X_add_number >= 8)
    {
      ill_op ();
      return p;
    }

  opcode += arg_r.X_add_number - 6;

  if (!(ins_ok & INS_Z80N))
    check_mach (INS_ROT_II_LD);

  emit_mx (prefix, opcode, 0, &arg_m);
  return p;
}

static void emit_sx(char prefix, char opcode, expressionS *arg_p)
{
    if (arg_p == NULL) {
        return;
    }

    if (arg_p->X_op == O_register || arg_p->X_op == O_md1) {
        emit_mx(prefix, opcode, 0, arg_p);
        return;
    }

    if (arg_p->X_md) {
        ill_op();
        return;
    }

    int size = prefix ? 2 : 1;
    char *q = frag_more(size);
    
    if (q == NULL) {
        return;
    }
    
    if (prefix) {
        *q++ = prefix;
    }
    
    *q = opcode ^ 0x46;
    emit_byte(arg_p, BFD_RELOC_8);
}

/* The operand s may be r, (hl), (ix+d), (iy+d), n.  */
static const char *
emit_s (char prefix, char opcode, const char *args)
{
  expressionS arg_s;
  const char *p;

  if (args == NULL)
    return NULL;

  p = parse_exp (args, &arg_s);
  if (p == NULL)
    return NULL;

  if (*p == ',' && 
      arg_s.X_md == 0 && 
      arg_s.X_op == O_register && 
      arg_s.X_add_number == REG_A)
    {
      if (!(ins_ok & INS_EZ80) && !sdcc_compat)
        {
          ill_op ();
          return p;
        }
      ++p;
      const char *new_p = parse_exp (p, &arg_s);
      if (new_p == NULL)
        return p;
      p = new_p;
    }
  
  emit_sx (prefix, opcode, &arg_s);
  return p;
}

static const char *
emit_sub (char prefix, char opcode, const char *args)
{
  expressionS arg_s;
  const char *p;

  if (!(ins_ok & INS_GBZ80))
    return emit_s (prefix, opcode, args);
  
  p = parse_exp (args, &arg_s);
  if (!p)
    return p;
    
  if (*p != ',')
    {
      error (_("bad instruction syntax"));
      return p + 1;
    }
  p++;

  if (arg_s.X_md != 0 || arg_s.X_op != O_register || arg_s.X_add_number != REG_A)
    {
      ill_op ();
      return p;
    }

  p = parse_exp (p, &arg_s);
  if (!p)
    return p;

  emit_sx (prefix, opcode, &arg_s);
  return p;
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
  
  if (reg.X_md != 0 || reg.X_op != O_register || reg.X_add_number != REG_A)
    {
      ill_op ();
      return p;
    }

  q = frag_more (2);
  if (q == NULL)
    return p;
    
  *q++ = 0xED;
  *q = 0x23;
  
  return p;
}

static const char *
emit_call (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  expressionS addr;
  const char *p;
  char *q;

  if (args == NULL)
    return NULL;

  p = parse_exp_not_indexed (args, &addr);
  
  if (p == NULL)
    return NULL;
    
  if (addr.X_md)
    {
      ill_op ();
      return p;
    }
  
  q = frag_more (1);
  if (q == NULL)
    return p;
    
  *q = opcode;
  emit_word (&addr);
  
  return p;
}

/* Operand may be rr, r, (hl), (ix+d), (iy+d).  */
static const char *
emit_incdec (char prefix, char opcode, const char * args)
{
  expressionS operand;
  int rnum;
  const char *p;
  char *q;

  p = parse_exp (args, &operand);
  if (!p)
    {
      ill_op ();
      return args;
    }

  if (operand.X_op == O_register && !operand.X_md)
    {
      rnum = operand.X_add_number;
      if (!(R_ARITH & rnum))
        {
          emit_mx (0, opcode, 3, &operand);
          return p;
        }

      if (rnum & R_INDEX)
        {
          q = frag_more (2);
          if (!q)
            {
              ill_op ();
              return p;
            }
          *q++ = (rnum & R_IX) ? 0xDD : 0xFD;
          *q = prefix + ((rnum & 3) << 4);
        }
      else
        {
          q = frag_more (1);
          if (!q)
            {
              ill_op ();
              return p;
            }
          *q = prefix + ((rnum & 3) << 4);
        }
    }
  else if (operand.X_op == O_md1 || operand.X_op == O_register)
    {
      emit_mx (0, opcode, 3, &operand);
    }
  else
    {
      ill_op ();
    }

  return p;
}

static const char *
emit_jr (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  expressionS addr;
  const char *p;
  char *q;

  if (args == NULL)
    {
      ill_op ();
      return NULL;
    }

  p = parse_exp_not_indexed (args, &addr);
  
  if (addr.X_md)
    {
      ill_op ();
      return p;
    }
  
  q = frag_more (1);
  if (q == NULL)
    {
      ill_op ();
      return p;
    }
  
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
  char *q;
  int rnum;

  p = parse_exp_not_indexed (args, &addr);
  
  if (!p)
    return NULL;
    
  if (!addr.X_md)
    {
      q = frag_more (1);
      if (!q)
        return NULL;
      *q = opcode;
      emit_word (&addr);
      return p;
    }

  rnum = addr.X_add_number;
  
  if (addr.X_op == O_register)
    {
      if ((rnum & ~R_INDEX) == REG_HL)
        {
          int is_indexed = (rnum & R_INDEX) != 0;
          int size = is_indexed ? 2 : 1;
          
          q = frag_more (size);
          if (!q)
            return NULL;
            
          if (is_indexed)
            {
              *q++ = (rnum & R_IX) ? 0xDD : 0xFD;
            }
          *q = prefix;
          return p;
        }
      
      if (rnum == REG_C && (ins_ok & INS_Z80N))
        {
          q = frag_more (2);
          if (!q)
            return NULL;
          *q++ = 0xED;
          *q = 0x98;
          return p;
        }
    }
  
  ill_op ();
  return p;
}

static const char *
emit_im (char prefix, char opcode, const char * args)
{
  expressionS mode;
  const char *p;
  char *q;
  int mode_value;

  p = parse_exp (args, & mode);
  if (p == NULL)
    return NULL;
    
  if (mode.X_md || (mode.X_op != O_constant))
    {
      ill_op ();
      return p;
    }

  mode_value = mode.X_add_number;
  
  if (mode_value == 1 || mode_value == 2)
    mode_value++;
  
  if (mode_value != 0 && mode_value != 3)
    {
      ill_op ();
      return p;
    }
  
  q = frag_more (2);
  if (q == NULL)
    return p;
    
  *q++ = prefix;
  *q = opcode + (8 * mode_value);
  
  return p;
}

static const char *
emit_pop (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  expressionS regp;
  const char *p;
  char *q;
  int rnum;

  p = parse_exp (args, &regp);
  
  if (regp.X_md || regp.X_op != O_register || !(regp.X_add_number & R_STACKABLE))
  {
    ill_op ();
    return p;
  }

  rnum = regp.X_add_number;
  
  if (rnum & R_INDEX)
  {
    q = frag_more (2);
    *q++ = (rnum & R_IX) ? 0xDD : 0xFD;
  }
  else
  {
    q = frag_more (1);
  }
  
  *q = opcode + ((rnum & 3) << 4);

  return p;
}

static const char *
emit_push (char prefix, char opcode, const char * args)
{
  expressionS arg;
  const char *p;
  char *q;

  p = parse_exp (args, &arg);
  
  if (arg.X_op == O_register)
    {
      return emit_pop (prefix, opcode, args);
    }

  if (arg.X_md || arg.X_op == O_md1 || !(ins_ok & INS_Z80N))
    {
      ill_op ();
      return p;
    }

  q = frag_more (2);
  if (!q)
    {
      return p;
    }
  
  q[0] = 0xED;
  q[1] = 0x8A;

  q = frag_more (2);
  if (!q)
    {
      return p;
    }
  
  fix_new_exp (frag_now, q - frag_now->fr_literal, 2, &arg, false,
               BFD_RELOC_Z80_16_BE);

  return p;
}

static const char *
emit_retcc (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  char cc;
  const char *p;
  char *q;

  if (args == NULL) {
    return NULL;
  }

  p = parse_cc (args, &cc);
  q = frag_more (1);
  
  if (q == NULL) {
    return args;
  }

  if (p != NULL) {
    *q = opcode + cc;
    return p;
  }
  
  *q = prefix;
  return args;
}

static const char *
emit_adc (char prefix, char opcode, const char * args)
{
  expressionS term;
  int rnum;
  const char *p;
  char *q;

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

  if (term.X_add_number == REG_A)
    {
      return emit_s (0, prefix, p);
    }

  if (term.X_add_number != REG_HL)
    {
      ill_op ();
      return p;
    }

  p = parse_exp (p, &term);
  if ((term.X_md) || (term.X_op != O_register))
    {
      ill_op ();
      return p;
    }

  rnum = term.X_add_number;
  if (R_ARITH != (rnum & (R_ARITH | R_INDEX)))
    {
      ill_op ();
      return p;
    }

  q = frag_more (2);
  *q++ = 0xED;
  *q = opcode + ((rnum & 3) << 4);
  return p;
}

static const char *
emit_add (char prefix, char opcode, const char * args)
{
  expressionS term;
  int lhs, rhs;
  const char *p;
  char *q;

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

  lhs = term.X_add_number;

  switch (lhs)
    {
    case REG_A:
      return emit_s (0, prefix, p);

    case REG_SP:
      p = parse_exp (p, &term);
      if (!(ins_ok & INS_GBZ80) || term.X_md || term.X_op == O_register)
        {
          ill_op ();
          return p;
        }
      q = frag_more (1);
      *q = 0xE8;
      emit_byte (&term, BFD_RELOC_Z80_DISP8);
      return p;

    case REG_BC:
    case REG_DE:
      if (!(ins_ok & INS_Z80N))
        {
          ill_op ();
          return p;
        }
      break;

    case REG_HL:
    case REG_IX:
    case REG_IY:
      break;

    default:
      ill_op ();
      return p;
    }

  p = parse_exp (p, &term);
  rhs = term.X_add_number;

  if (term.X_md != 0 || term.X_op == O_md1)
    {
      ill_op ();
      return p;
    }

  if ((term.X_op == O_register) && (rhs & R_ARITH) && 
      (rhs == lhs || (rhs & ~R_INDEX) != REG_HL))
    {
      q = frag_more ((lhs & R_INDEX) ? 2 : 1);
      if (lhs & R_INDEX)
        *q++ = (lhs & R_IX) ? 0xDD : 0xFD;
      *q = opcode + ((rhs & 3) << 4);
      return p;
    }

  if (!(lhs & R_INDEX) && (ins_ok & INS_Z80N))
    {
      if (term.X_op == O_register && rhs == REG_A)
        {
          q = frag_more (2);
          *q++ = 0xED;
          *q = 0x33 - (lhs & 3);
          return p;
        }
      
      if (term.X_op != O_register && term.X_op != O_md1)
        {
          q = frag_more (2);
          *q++ = 0xED;
          *q = 0x36 - (lhs & 3);
          emit_word (&term);
          return p;
        }
    }

  ill_op ();
  return p;
}

static const char *
emit_bit (char prefix, char opcode, const char * args)
{
  expressionS b;
  int bn;
  const char *p;

  if (args == NULL)
    {
      ill_op ();
      return NULL;
    }

  p = parse_exp (args, &b);
  if (p == NULL || *p != ',')
    {
      error (_("bad instruction syntax"));
      return NULL;
    }
  
  p++;

  if (b.X_md || b.X_op != O_constant)
    {
      ill_op ();
      return NULL;
    }

  bn = b.X_add_number;
  if (bn < 0 || bn >= 8)
    {
      ill_op ();
      return NULL;
    }

  if (opcode == 0x40)
    {
      p = emit_m (prefix, opcode + (bn << 3), p);
    }
  else
    {
      p = emit_mr (prefix, opcode + (bn << 3), p);
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

  if (!args)
    {
      error (_("bad instruction syntax"));
      return args;
    }

  p = parse_exp (args, &r1);
  if (!p || *p != ',')
    {
      error (_("bad instruction syntax"));
      return p;
    }
  
  p++;
  p = parse_exp (p, &r2);
  
  if (r1.X_md != 0 || 
      r1.X_op != O_register || 
      r1.X_add_number != REG_DE)
    {
      ill_op ();
      return p;
    }
  
  if (r2.X_md != 0 || 
      r2.X_op != O_register || 
      r2.X_add_number != REG_B)
    {
      ill_op ();
      return p;
    }
  
  q = frag_more (2);
  if (!q)
    {
      error (_("memory allocation failed"));
      return p;
    }
  
  q[0] = prefix;
  q[1] = opcode;
  
  return p;
}

static const char *emit_jpcc(char prefix, char opcode, const char *args)
{
    if (!args) {
        return NULL;
    }

    char cc;
    const char *p = parse_cc(args, &cc);
    
    if (!p) {
        return (prefix == (char)0xC3) 
            ? emit_jp(0xE9, prefix, args)
            : emit_call(0, prefix, args);
    }
    
    if (*p != ',') {
        return (prefix == (char)0xC3)
            ? emit_jp(0xE9, prefix, args)
            : emit_call(0, prefix, args);
    }
    
    p++;
    return emit_call(0, opcode + cc, p);
}

static const char *emit_jrcc(char prefix, char opcode, const char *args)
{
    if (!args) {
        return NULL;
    }

    char cc = 0;
    const char *p = parse_cc(args, &cc);
    
    if (!p) {
        return emit_jr(0, prefix, args);
    }
    
    if (*p != ',') {
        return emit_jr(0, prefix, args);
    }
    
    p++;
    
    if (cc > 24) {
        error(_("condition code invalid for jr"));
        return NULL;
    }
    
    return emit_jr(0, opcode + cc, p);
}

static const char *
emit_ex (char prefix_in ATTRIBUTE_UNUSED,
	 char opcode_in ATTRIBUTE_UNUSED, const char * args)
{
  expressionS op;
  const char * p;
  char prefix = 0;
  char opcode = 0;

  p = parse_exp_not_indexed (args, &op);
  if (!p)
    return args;
    
  p = skip_space (p);
  if (!p || *p != ',')
    {
      error (_("bad instruction syntax"));
      return p ? p + 1 : args;
    }
  p++;

  if (op.X_op != O_register)
    {
      ill_op ();
      return p;
    }

  int reg_value = op.X_add_number | (op.X_md ? 0x8000 : 0);
  
  switch (reg_value)
    {
    case REG_AF:
      if (p[0] && TOLOWER (p[0]) == 'a' && 
          p[1] && TOLOWER (p[1]) == 'f')
        {
          p += 2;
          if (*p == '`')
            p++;
          opcode = 0x08;
        }
      break;
      
    case REG_DE:
      if (p[0] && TOLOWER (p[0]) == 'h' && 
          p[1] && TOLOWER (p[1]) == 'l')
        {
          p += 2;
          opcode = 0xEB;
        }
      break;
      
    case (REG_SP | 0x8000):
      {
        const char *temp_p = parse_exp (p, &op);
        if (temp_p && 
            op.X_op == O_register &&
            op.X_md == 0 &&
            (op.X_add_number & ~R_INDEX) == REG_HL)
          {
            p = temp_p;
            opcode = 0xE3;
            if (R_INDEX & op.X_add_number)
              prefix = (R_IX & op.X_add_number) ? 0xDD : 0xFD;
          }
      }
      break;
      
    default:
      break;
    }

  if (opcode)
    emit_insn (prefix, opcode, p);
  else
    ill_op ();

  return p;
}

static const char *
emit_in (char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED,
	const char * args)
{
  expressionS reg, port;
  const char *p;
  char *q;

  p = parse_exp (args, &reg);
  if (reg.X_md && reg.X_op == O_register && reg.X_add_number == REG_C)
    {
      port = reg;
      reg.X_md = 0;
      reg.X_add_number = REG_F;
    }
  else
    {
      if (*p++ != ',')
	{
	  error (_("bad instruction syntax"));
	  return p;
	}
      p = parse_exp (p, &port);
    }
  
  if (!reg.X_md || reg.X_op != O_register)
    {
      ill_op ();
      return p;
    }
    
  if (reg.X_add_number > 7 && reg.X_add_number != REG_F)
    {
      ill_op ();
      return p;
    }
    
  if (!port.X_md)
    {
      ill_op ();
      return p;
    }
    
  if (port.X_op != O_md1 && port.X_op != O_register)
    {
      if (reg.X_add_number != REG_A)
        {
          ill_op ();
          return p;
        }
      q = frag_more (1);
      *q = 0xDB;
      emit_byte (&port, BFD_RELOC_8);
      return p;
    }
    
  if (port.X_add_number != REG_C && port.X_add_number != REG_BC)
    {
      ill_op ();
      return p;
    }
    
  if (port.X_add_number == REG_BC && !(ins_ok & INS_EZ80))
    {
      ill_op ();
      return p;
    }
    
  if (reg.X_add_number == REG_F && !(ins_ok & (INS_R800|INS_Z80N)))
    {
      check_mach (INS_IN_F_C);
    }
    
  q = frag_more (2);
  *q++ = 0xED;
  *q = 0x40 | ((reg.X_add_number & 7) << 3);
  
  return p;
}

static const char *
emit_in0 (char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED,
        const char * args)
{
  expressionS reg, port;
  const char *p;
  char *q;

  p = parse_exp (args, &reg);
  if (*p++ != ',')
    {
      error (_("bad instruction syntax"));
      return p;
    }

  p = parse_exp (p, &port);
  
  if (reg.X_md != 0 || 
      reg.X_op != O_register || 
      reg.X_add_number > 7 ||
      !port.X_md ||
      port.X_op == O_md1 ||
      port.X_op == O_register)
    {
      ill_op ();
      return p;
    }

  q = frag_more (2);
  *q++ = 0xED;
  *q = 0x00 | (reg.X_add_number << 3);
  emit_byte (&port, BFD_RELOC_8);
  
  return p;
}

static const char *
emit_out (char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED,
	 const char * args)
{
  expressionS reg, port;
  const char *p;
  char *q;

  p = parse_exp (args, &port);
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
          q = frag_more (1);
          *q = 0xD3;
          emit_byte (&port, BFD_RELOC_8);
        }
      else
        {
          ill_op ();
        }
      return p;
    }
  
  if (REG_C == port.X_add_number || port.X_add_number == REG_BC)
    {
      if (port.X_add_number == REG_BC && !(ins_ok & INS_EZ80))
        {
          ill_op ();
          return p;
        }
      q = frag_more (2);
      *q++ = 0xED;
      *q = 0x41 | (reg.X_add_number << 3);
    }
  else
    {
      ill_op ();
    }
  
  return p;
}

static const char *
emit_out0(char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED,
          const char *args)
{
    expressionS reg, port;
    const char *p;
    char *q;

    p = parse_exp(args, &port);
    if (*p++ != ',') {
        error(_("bad instruction syntax"));
        return p;
    }

    p = parse_exp(p, &reg);
    
    if (!is_valid_out0_instruction(&port, &reg)) {
        ill_op();
        return p;
    }

    q = frag_more(2);
    *q++ = 0xED;
    *q = 0x01 | (reg.X_add_number << 3);
    emit_byte(&port, BFD_RELOC_8);
    
    return p;
}

static int is_valid_out0_instruction(const expressionS *port, const expressionS *reg)
{
    return port->X_md != 0 &&
           port->X_op != O_register &&
           port->X_op != O_md1 &&
           reg->X_md == 0 &&
           reg->X_op == O_register &&
           reg->X_add_number <= 7;
}

static const char *
emit_rst (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  expressionS addr;
  const char *p;
  char *q;
  const int RST_MASK = 0x38;
  const int RST_INVALID_MASK = ~RST_MASK;

  p = parse_exp_not_indexed (args, &addr);
  if (p == NULL)
    {
      return NULL;
    }

  if (addr.X_op != O_constant)
    {
      error ("rst needs constant address");
      return p;
    }

  if (addr.X_add_number & RST_INVALID_MASK)
    {
      ill_op ();
      return p;
    }

  q = frag_more (1);
  if (q == NULL)
    {
      return p;
    }

  *q = opcode + (addr.X_add_number & RST_MASK);
  return p;
}

/* For 8-bit indirect load to memory instructions like: LD (HL),n or LD (ii+d),n.  */
static void
emit_ld_m_n (expressionS *dst, expressionS *src)
{
  char *q;
  char prefix;
  expressionS dst_offset;

  switch (dst->X_add_number)
    {
    case REG_HL: 
      prefix = 0x00; 
      break;
    case REG_IX: 
      prefix = 0xDD; 
      break;
    case REG_IY: 
      prefix = 0xFD; 
      break;
    default:
      ill_op ();
      return;
    }

  if (prefix != 0x00)
    {
      q = frag_more (2);
      *q++ = prefix;
      *q = 0x36;
      
      dst_offset = *dst;
      dst_offset.X_op = O_symbol;
      dst_offset.X_add_number = 0;
      emit_byte (&dst_offset, BFD_RELOC_Z80_DISP8);
    }
  else
    {
      q = frag_more (1);
      *q = 0x36;
    }
  
  emit_byte (src, BFD_RELOC_8);
}

/* For 8-bit load register to memory instructions: LD (<expression>),r.  */
static void
emit_ld_m_r (expressionS *dst, expressionS *src)
{
  char *q;
  char prefix = 0;
  expressionS dst_offset;

  if (dst->X_op == O_md1)
    {
      if (ins_ok & INS_GBZ80)
        {
          if (src->X_op == O_register && src->X_add_number == REG_A)
            {
              *frag_more (1) = (dst->X_add_number == REG_HL) ? 0x22 : 0x32;
              return;
            }
          ill_op ();
          return;
        }
      prefix = (dst->X_add_number == REG_IX) ? 0xDD : 0xFD;
    }

  if (dst->X_op == O_md1 || dst->X_op == O_register)
    {
      if (dst->X_add_number == REG_BC || dst->X_add_number == REG_DE)
        {
          if (src->X_add_number == REG_A)
            {
              q = frag_more (1);
              *q = 0x02 | ((dst->X_add_number & 3) << 4);
              return;
            }
        }
      else if (dst->X_add_number == REG_IX || dst->X_add_number == REG_IY || dst->X_add_number == REG_HL)
        {
          if (src->X_add_number <= 7)
            {
              q = frag_more (prefix ? 2 : 1);
              if (prefix)
                *q++ = prefix;
              *q = 0x70 | src->X_add_number;
              if (prefix)
                {
                  dst_offset = *dst;
                  dst_offset.X_op = O_symbol;
                  dst_offset.X_add_number = 0;
                  emit_byte (&dst_offset, BFD_RELOC_Z80_DISP8);
                }
              return;
            }
        }
    }
  else if (src->X_add_number == REG_A)
    {
      q = frag_more (1);
      *q = (ins_ok & INS_GBZ80) ? 0xEA : 0x32;
      emit_word (dst);
      return;
    }
  
  ill_op ();
}

/* For 16-bit load register to memory instructions: LD (<expression>),rr.  */
static void
emit_ld_m_rr (expressionS *dst, expressionS *src)
{
  char *q;
  int prefix = 0;
  int opcode = 0;
  expressionS dst_offset;

  if (dst->X_op == O_md1 || dst->X_op == O_register)
    {
      if (!(ins_ok & INS_EZ80))
        {
          ill_op ();
          return;
        }
      
      switch (dst->X_add_number)
        {
        case REG_IX: 
          prefix = 0xDD; 
          break;
        case REG_IY: 
          prefix = 0xFD; 
          break;
        case REG_HL: 
          prefix = 0xED; 
          break;
        default:
          ill_op ();
          return;
        }
      
      switch (src->X_add_number)
        {
        case REG_BC: 
          opcode = 0x0F; 
          break;
        case REG_DE: 
          opcode = 0x1F; 
          break;
        case REG_HL: 
          opcode = 0x2F; 
          break;
        case REG_IX: 
          opcode = (prefix != 0xFD) ? 0x3F : 0x3E; 
          break;
        case REG_IY: 
          opcode = (prefix != 0xFD) ? 0x3E : 0x3F; 
          break;
        default:
          ill_op ();
          return;
        }
      
      q = frag_more (prefix ? 2 : 1);
      if (prefix)
        *q++ = prefix;
      *q = opcode;
      
      if (prefix == 0xFD || prefix == 0xDD)
        {
          dst_offset = *dst;
          dst_offset.X_op = O_symbol;
          dst_offset.X_add_number = 0;
          emit_byte (&dst_offset, BFD_RELOC_Z80_DISP8);
        }
    }
  else
    {
      if (ins_ok & INS_GBZ80)
        {
          if (src->X_add_number != REG_SP)
            {
              ill_op ();
              return;
            }
          prefix = 0x00;
          opcode = 0x08;
        }
      else
        {
          switch (src->X_add_number)
            {
            case REG_BC: 
              prefix = 0xED; 
              opcode = 0x43; 
              break;
            case REG_DE: 
              prefix = 0xED; 
              opcode = 0x53; 
              break;
            case REG_HL: 
              prefix = 0x00; 
              opcode = 0x22; 
              break;
            case REG_IX: 
              prefix = 0xDD; 
              opcode = 0x22; 
              break;
            case REG_IY: 
              prefix = 0xFD; 
              opcode = 0x22; 
              break;
            case REG_SP: 
              prefix = 0xED; 
              opcode = 0x73; 
              break;
            default:
              ill_op ();
              return;
            }
        }
      
      q = frag_more (prefix ? 2 : 1);
      if (prefix)
        *q++ = prefix;
      *q = opcode;
      emit_word (dst);
    }
}

static void
emit_ld_r_m (expressionS *dst, expressionS *src)
{
  char *q;
  char prefix = 0;
  char opcode = 0;
  expressionS src_offset;

  if (dst->X_add_number == REG_A && src->X_op == O_register)
    {
      if (src->X_add_number == REG_BC)
        opcode = 0x0A;
      else if (src->X_add_number == REG_DE)
        opcode = 0x1A;
      
      if (opcode != 0)
        {
          q = frag_more (1);
          if (q != NULL)
            *q = opcode;
          return;
        }
    }

  if (src->X_op == O_md1)
    {
      if ((ins_ok & INS_GBZ80) != 0)
        {
          if (dst->X_op == O_register && dst->X_add_number == REG_A)
            {
              q = frag_more (1);
              if (q != NULL)
                *q = (src->X_add_number == REG_HL) ? 0x2A : 0x3A;
            }
          else
            {
              ill_op ();
            }
          return;
        }
      src->X_op = O_register;
    }

  if (src->X_op == O_register)
    {
      if (dst->X_add_number > 7)
        {
          ill_op ();
          return;
        }
      
      opcode = 0x46;
      
      if (src->X_add_number == REG_HL)
        prefix = 0x00;
      else if (src->X_add_number == REG_IX)
        prefix = 0xDD;
      else if (src->X_add_number == REG_IY)
        prefix = 0xFD;
      else
        {
          ill_op ();
          return;
        }
      
      q = frag_more (prefix ? 2 : 1);
      if (q == NULL)
        return;
        
      if (prefix)
        *q++ = prefix;
      *q = opcode | ((dst->X_add_number & 7) << 3);
      
      if (prefix)
        {
          src_offset = *src;
          src_offset.X_op = O_symbol;
          src_offset.X_add_number = 0;
          emit_byte (&src_offset, BFD_RELOC_Z80_DISP8);
        }
    }
  else
    {
      if (dst->X_add_number == REG_A)
        {
          q = frag_more (1);
          if (q != NULL)
            *q = ((ins_ok & INS_GBZ80) != 0) ? 0xFA : 0x3A;
          emit_word (src);
        }
      else
        {
          ill_op ();
        }
    }
}

static void
emit_ld_r_n (expressionS *dst, expressionS *src)
{
  char *q;
  char prefix = 0;
  int reg_num = dst->X_add_number;

  if (reg_num == (REG_H | R_IX) || reg_num == (REG_L | R_IX))
    {
      prefix = 0xDD;
    }
  else if (reg_num == (REG_H | R_IY) || reg_num == (REG_L | R_IY))
    {
      prefix = 0xFD;
    }
  else if (reg_num != REG_A && reg_num != REG_B && reg_num != REG_C &&
           reg_num != REG_D && reg_num != REG_E && reg_num != REG_H &&
           reg_num != REG_L)
    {
      ill_op ();
      return;
    }

  q = frag_more (prefix ? 2 : 1);
  if (prefix)
    {
      if (ins_ok & INS_GBZ80)
        {
          ill_op ();
          return;
        }
      if (!(ins_ok & (INS_EZ80 | INS_R800 | INS_Z80N)))
        {
          check_mach (INS_IDX_HALF);
        }
      *q++ = prefix;
    }
  *q = 0x06 | ((reg_num & 7) << 3);
  emit_byte (src, BFD_RELOC_8);
}

static void
emit_ld_r_r (expressionS *dst, expressionS *src)
{
  char *q;
  int prefix = 0;
  int opcode = 0;
  int ii_halves = 0;

  if (!dst || !src) {
    ill_op();
    return;
  }

  if (dst->X_add_number == REG_SP) {
    handle_ld_sp_src(src, &prefix, &opcode);
  } else if (dst->X_add_number == REG_HL) {
    handle_ld_hl_src(src, &prefix, &opcode);
  } else if (dst->X_add_number == REG_I) {
    handle_ld_i_src(src, &prefix, &opcode);
  } else if (dst->X_add_number == REG_MB) {
    handle_ld_mb_src(src, &prefix, &opcode);
  } else if (dst->X_add_number == REG_R) {
    handle_ld_r_src(src, &prefix, &opcode);
  } else if (dst->X_add_number == REG_A) {
    handle_ld_a_src(src, &prefix, &opcode);
  } else {
    handle_ld_general_dst(dst, &prefix, &ii_halves);
  }

  if (opcode == 0) {
    compute_opcode(dst, src, &prefix, &opcode, &ii_halves);
  }

  validate_and_emit(prefix, opcode, ii_halves);
}

static void handle_ld_sp_src(expressionS *src, int *prefix, int *opcode)
{
  switch (src->X_add_number) {
    case REG_HL: *prefix = 0x00; break;
    case REG_IX: *prefix = 0xDD; break;
    case REG_IY: *prefix = 0xFD; break;
    default: ill_op(); return;
  }
  *opcode = 0xF9;
}

static void handle_ld_hl_src(expressionS *src, int *prefix, int *opcode)
{
  if (!(ins_ok & INS_EZ80) || src->X_add_number != REG_I) {
    ill_op();
    return;
  }
  if (cpu_mode < 1) {
    error(_("ADL mode instruction"));
    return;
  }
  *prefix = 0xED;
  *opcode = 0xD7;
}

static void handle_ld_i_src(expressionS *src, int *prefix, int *opcode)
{
  if (src->X_add_number == REG_HL) {
    if (!(ins_ok & INS_EZ80)) {
      ill_op();
      return;
    }
    if (cpu_mode < 1) {
      error(_("ADL mode instruction"));
      return;
    }
    *prefix = 0xED;
    *opcode = 0xC7;
  } else if (src->X_add_number == REG_A) {
    *prefix = 0xED;
    *opcode = 0x47;
  } else {
    ill_op();
  }
}

static void handle_ld_mb_src(expressionS *src, int *prefix, int *opcode)
{
  if (!(ins_ok & INS_EZ80) || src->X_add_number != REG_A) {
    ill_op();
    return;
  }
  if (cpu_mode < 1) {
    error(_("ADL mode instruction"));
    return;
  }
  *prefix = 0xED;
  *opcode = 0x6D;
}

static void handle_ld_r_src(expressionS *src, int *prefix, int *opcode)
{
  if (src->X_add_number == REG_A) {
    *prefix = 0xED;
    *opcode = 0x4F;
  } else {
    ill_op();
  }
}

static void handle_ld_a_src(expressionS *src, int *prefix, int *opcode)
{
  if (src->X_add_number == REG_I) {
    *prefix = 0xED;
    *opcode = 0x57;
  } else if (src->X_add_number == REG_R) {
    *prefix = 0xED;
    *opcode = 0x5F;
  } else if (src->X_add_number == REG_MB) {
    if (!(ins_ok & INS_EZ80)) {
      ill_op();
      return;
    }
    if (cpu_mode < 1) {
      error(_("ADL mode instruction"));
      return;
    }
    *prefix = 0xED;
    *opcode = 0x6E;
  }
}

static void handle_ld_general_dst(expressionS *dst, int *prefix, int *ii_halves)
{
  switch (dst->X_add_number) {
    case REG_B:
    case REG_C:
    case REG_D:
    case REG_E:
    case REG_H:
    case REG_L:
      *prefix = 0x00;
      break;
    case REG_H|R_IX:
    case REG_L|R_IX:
      *prefix = 0xDD;
      *ii_halves = 1;
      break;
    case REG_H|R_IY:
    case REG_L|R_IY:
      *prefix = 0xFD;
      *ii_halves = 1;
      break;
    default:
      ill_op();
  }
}

static void compute_opcode(expressionS *dst, expressionS *src, int *prefix, int *opcode, int *ii_halves)
{
  switch (src->X_add_number) {
    case REG_A:
    case REG_B:
    case REG_C:
    case REG_D:
    case REG_E:
      break;
    case REG_H:
    case REG_L:
      if (*prefix != 0) {
        ill_op();
        return;
      }
      break;
    case REG_H|R_IX:
    case REG_L|R_IX:
      if (*prefix == 0xFD || dst->X_add_number == REG_H || dst->X_add_number == REG_L) {
        ill_op();
        return;
      }
      *prefix = 0xDD;
      *ii_halves = 1;
      break;
    case REG_H|R_IY:
    case REG_L|R_IY:
      if (*prefix == 0xDD || dst->X_add_number == REG_H || dst->X_add_number == REG_L) {
        ill_op();
        return;
      }
      *prefix = 0xFD;
      *ii_halves = 1;
      break;
    default:
      ill_op();
      return;
  }
  *opcode = 0x40 + ((dst->X_add_number & 7) << 3) + (src->X_add_number & 7);
}

static void validate_and_emit(int prefix, int opcode, int ii_halves)
{
  char *q;
  
  if ((ins_ok & INS_GBZ80) && prefix != 0) {
    ill_op();
    return;
  }
  
  if (ii_halves && !(ins_ok & (INS_EZ80|INS_R800|INS_Z80N))) {
    check_mach(INS_IDX_HALF);
  }
  
  if (prefix == 0 && (ins_ok & INS_EZ80)) {
    switch (opcode) {
      case 0x40:
      case 0x49:
      case 0x52:
      case 0x5B:
        as_warn(_("unsupported instruction, assembled as NOP"));
        opcode = 0x00;
        break;
      default:
        break;
    }
  }
  
  q = frag_more(prefix ? 2 : 1);
  if (prefix) {
    *q++ = prefix;
  }
  *q = opcode;
}

static void
emit_ld_rr_m (expressionS *dst, expressionS *src)
{
  char *q;
  int prefix = 0;
  int opcode = 0;
  expressionS src_offset;

  if (ins_ok & INS_GBZ80)
    {
      ill_op ();
      return;
    }

  if (src->X_op == O_md1 || src->X_op == O_register)
    {
      if (!(ins_ok & INS_EZ80))
        {
          ill_op ();
          return;
        }

      if (src->X_op == O_md1)
        prefix = (src->X_add_number == REG_IX) ? 0xDD : 0xFD;
      else
        prefix = 0xED;

      switch (dst->X_add_number)
        {
        case REG_BC:
          opcode = 0x07;
          break;
        case REG_DE:
          opcode = 0x17;
          break;
        case REG_HL:
          opcode = 0x27;
          break;
        case REG_IX:
          opcode = (prefix == 0xED || prefix == 0xDD) ? 0x37 : 0x31;
          break;
        case REG_IY:
          opcode = (prefix == 0xED || prefix == 0xDD) ? 0x31 : 0x37;
          break;
        default:
          ill_op ();
          return;
        }

      q = frag_more (2);
      *q++ = prefix;
      *q = opcode;

      if (prefix != 0xED)
        {
          src_offset = *src;
          src_offset.X_op = O_symbol;
          src_offset.X_add_number = 0;
          emit_byte (&src_offset, BFD_RELOC_Z80_DISP8);
        }
    }
  else
    {
      switch (dst->X_add_number)
        {
        case REG_BC:
          prefix = 0xED;
          opcode = 0x4B;
          break;
        case REG_DE:
          prefix = 0xED;
          opcode = 0x5B;
          break;
        case REG_HL:
          prefix = 0x00;
          opcode = 0x2A;
          break;
        case REG_SP:
          prefix = 0xED;
          opcode = 0x7B;
          break;
        case REG_IX:
          prefix = 0xDD;
          opcode = 0x2A;
          break;
        case REG_IY:
          prefix = 0xFD;
          opcode = 0x2A;
          break;
        default:
          ill_op ();
          return;
        }

      q = frag_more (prefix ? 2 : 1);
      if (prefix)
        *q++ = prefix;
      *q = opcode;
      emit_word (src);
    }
}

static void
emit_ld_rr_nn (expressionS *dst, expressionS *src)
{
  char *q;
  int prefix = 0x00;
  int opcode = 0x21;
  
  if (dst == NULL || src == NULL)
    {
      ill_op ();
      return;
    }
  
  switch (dst->X_add_number)
    {
    case REG_IX:
      prefix = 0xDD;
      break;
    case REG_IY:
      prefix = 0xFD;
      break;
    case REG_HL:
      break;
    case REG_BC:
    case REG_DE:
    case REG_SP:
      opcode = 0x01 + ((dst->X_add_number & 3) << 4);
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
  
  int size = (prefix != 0x00) ? 2 : 1;
  q = frag_more (size);
  
  if (q == NULL)
    {
      ill_op ();
      return;
    }
  
  if (prefix != 0x00)
    {
      *q = prefix;
      q++;
    }
  
  *q = opcode;
  emit_word (src);
}

static const char *
emit_ld (char prefix_in ATTRIBUTE_UNUSED, char opcode_in ATTRIBUTE_UNUSED,
	const char * args)
{
  expressionS dst, src;
  const char *p;

  p = parse_exp (args, & dst);
  if (*p++ != ',')
    error (_("bad instruction syntax"));
  p = parse_exp (p, & src);

  if (dst.X_md)
    {
      emit_ld_to_memory(&dst, &src);
    }
  else if (dst.X_op == O_register)
    {
      emit_ld_to_register(&dst, &src);
    }
  else
    {
      ill_op ();
    }

  return p;
}

static void emit_ld_to_memory(expressionS *dst, expressionS *src)
{
  if (src->X_op == O_register)
    {
      if (src->X_add_number <= 7)
        emit_ld_m_r (dst, src);
      else
        emit_ld_m_rr (dst, src);
    }
  else
    {
      emit_ld_m_n (dst, src);
    }
}

static void emit_ld_to_register(expressionS *dst, expressionS *src)
{
  if (src->X_md)
    {
      if (dst->X_add_number <= 7)
        emit_ld_r_m (dst, src);
      else
        emit_ld_rr_m (dst, src);
    }
  else if (src->X_op == O_register)
    {
      emit_ld_r_r (dst, src);
    }
  else if ((dst->X_add_number & ~R_INDEX) <= 7)
    {
      emit_ld_r_n (dst, src);
    }
  else
    {
      emit_ld_rr_nn (dst, src);
    }
}

static const char *
emit_lddldi (char prefix, char opcode, const char * args)
{
  expressionS dst, src;
  const char *p;
  char *q;
  char new_opcode;

  if (!(ins_ok & INS_GBZ80))
    return emit_insn (prefix, opcode, args);

  p = parse_exp (args, &dst);
  if (!p || *p != ',')
    {
      error (_("bad instruction syntax"));
      return p;
    }
  
  p++;
  p = parse_exp (p, &src);
  if (!p)
    return p;

  if (dst.X_op != O_register || src.X_op != O_register)
    {
      ill_op ();
      return p;
    }

  new_opcode = ((opcode & 0x08) << 1) | 0x22;

  if (dst.X_md != 0 && 
      dst.X_add_number == REG_HL && 
      src.X_md == 0 && 
      src.X_add_number == REG_A)
    {
      new_opcode |= 0x00;
    }
  else if (dst.X_md == 0 && 
           dst.X_add_number == REG_A && 
           src.X_md != 0 && 
           src.X_add_number == REG_HL)
    {
      new_opcode |= 0x08;
    }
  else
    {
      ill_op ();
      return p;
    }

  q = frag_more (1);
  if (q)
    *q = new_opcode;
    
  return p;
}

static const char *
emit_ldh (char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED,
        const char * args)
{
  expressionS dst, src;
  const char *p;

  p = parse_exp (args, & dst);
  if (*p++ != ',')
    {
      error (_("bad instruction syntax"));
      return p;
    }

  p = parse_exp (p, & src);
  
  if (dst.X_md == 0 && dst.X_op == O_register && dst.X_add_number == REG_A)
    {
      if (src.X_md == 0 || src.X_op == O_md1)
        {
          ill_op ();
          return p;
        }
      
      if (src.X_op == O_register)
        {
          if (src.X_add_number == REG_C)
            *frag_more (1) = 0xF2;
          else
            ill_op ();
        }
      else
        {
          *frag_more (1) = 0xF0;
          emit_byte (& src, BFD_RELOC_8);
        }
    }
  else if (src.X_md == 0 && src.X_op == O_register && src.X_add_number == REG_A)
    {
      if (dst.X_md == 0 || dst.X_op == O_md1)
        {
          ill_op ();
          return p;
        }
      
      if (dst.X_op == O_register)
        {
          if (dst.X_add_number == REG_C)
            *frag_more (1) = 0xE2;
          else
            ill_op ();
        }
      else
        {
          *frag_more (1) = 0xE0;
          emit_byte (& dst, BFD_RELOC_8);
        }
    }
  else
    {
      ill_op ();
    }

  return p;
}

static const char *
emit_ldhl (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  expressionS dst, src;
  const char *p;
  char *q;
  
  p = parse_exp (args, &dst);
  if (p == NULL)
    return NULL;
    
  if (*p != ',')
    {
      error (_("bad instruction syntax"));
      return p + (*p != '\0');
    }
  p++;

  p = parse_exp (p, &src);
  if (p == NULL)
    return NULL;
    
  if (dst.X_md != 0 || 
      dst.X_op != O_register || 
      dst.X_add_number != REG_SP ||
      src.X_md != 0 || 
      src.X_op == O_register || 
      src.X_op == O_md1)
    {
      ill_op ();
      return p;
    }
    
  q = frag_more (1);
  if (q == NULL)
    return NULL;
    
  *q = opcode;
  emit_byte (&src, BFD_RELOC_Z80_DISP8);
  
  return p;
}

static const char *
parse_lea_pea_args (const char * args, expressionS *op)
{
  const char *p;
  
  if (!args || !op)
    return NULL;
    
  p = parse_exp (args, op);
  
  if (!p)
    return NULL;
    
  if (!sdcc_compat || *p != ',' || op->X_op != O_register)
    return p;
    
  expressionS off;
  const char *next_p = parse_exp (p + 1, &off);
  
  if (!next_p)
    return p;
    
  op->X_op = O_add;
  op->X_add_symbol = make_expr_symbol (&off);
  
  return next_p;
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
  switch (rnum)
    {
    case REG_BC:
    case REG_DE:
    case REG_HL:
      opcode = 0x02 | ((rnum & 0x03) << 4);
      break;
    case REG_IX:
      opcode = 0x32;
      break;
    case REG_IY:
      opcode = 0x33;
      break;
    default:
      ill_op ();
    }

  if (*p++ != ',')
    error (_("bad instruction syntax"));

  p = parse_lea_pea_args (p, & src);
  if (src.X_md != 0)
    ill_op ();

  rnum = src.X_add_number;
  if (src.X_op == O_register)
    {
      src.X_add_symbol = zero;
      src.X_op = O_add;
    }
  else if (src.X_op != O_add)
    {
      ill_op ();
    }

  if (rnum == REG_IX)
    {
      if (opcode == 0x33)
        opcode = 0x55;
    }
  else if (rnum == REG_IY)
    {
      if (opcode == 0x32)
        opcode = 0x54;
      else
        opcode |= 0x01;
    }

  q = frag_more (2);
  *q++ = prefix;
  *q = opcode;

  src.X_op = O_symbol;
  src.X_add_number = 0;
  emit_byte (& src, BFD_RELOC_Z80_DISP8);

  return p;
}

static const char *
emit_mlt (char prefix, char opcode, const char * args)
{
  expressionS arg;
  const char *p;
  char *q;

  p = parse_exp (args, &arg);
  
  if (arg.X_md != 0 || arg.X_op != O_register || !(arg.X_add_number & R_ARITH))
    {
      ill_op ();
      return p;
    }

  q = frag_more (2);
  if (!q)
    {
      return p;
    }

  if (ins_ok & INS_Z80N)
    {
      if (arg.X_add_number != REG_DE)
        {
          ill_op ();
          return p;
        }
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
static const char *
emit_mul (char prefix, char opcode, const char * args)
{
  expressionS r1, r2;
  const char *p;
  char *q;

  if (args == NULL)
    {
      error (_("bad instruction syntax"));
      return NULL;
    }

  p = parse_exp (args, &r1);
  if (p == NULL || *p != ',')
    {
      error (_("bad instruction syntax"));
      return p;
    }
  
  p++;
  p = parse_exp (p, &r2);
  if (p == NULL)
    {
      error (_("bad instruction syntax"));
      return NULL;
    }

  if (r1.X_md != 0 || r1.X_op != O_register || r1.X_add_number != REG_D)
    {
      ill_op ();
      return p;
    }

  if (r2.X_md != 0 || r2.X_op != O_register || r2.X_add_number != REG_E)
    {
      ill_op ();
      return p;
    }

  q = frag_more (2);
  if (q == NULL)
    {
      error (_("memory allocation failed"));
      return p;
    }

  *q++ = prefix;
  *q = opcode;

  return p;
}

static const char *
emit_nextreg (char prefix, char opcode ATTRIBUTE_UNUSED, const char * args)
{
  expressionS rr, nn;
  const char *p;
  char *q;

  p = parse_exp (args, &rr);
  if (*p++ != ',')
    error (_("bad instruction syntax"));
  
  p = parse_exp (p, &nn);
  
  if (rr.X_md != 0 || rr.X_op == O_register || rr.X_op == O_md1)
    ill_op ();
    
  if (nn.X_md != 0 || nn.X_op == O_md1)
    ill_op ();
  
  q = frag_more (2);
  *q++ = prefix;
  emit_byte (&rr, BFD_RELOC_8);
  
  if (nn.X_op == O_register)
    {
      if (nn.X_add_number == REG_A)
        {
          *q = 0x92;
        }
      else
        {
          ill_op ();
        }
    }
  else
    {
      *q = 0x91;
      emit_byte (&nn, BFD_RELOC_8);
    }
  
  return p;
}

static const char *
emit_pea (char prefix, char opcode, const char * args)
{
  expressionS arg;
  const char *p;
  char *q;

  p = parse_lea_pea_args (args, &arg);
  
  if (p == NULL)
    {
      ill_op ();
      return NULL;
    }
  
  if (arg.X_md != 0)
    {
      ill_op ();
      return p;
    }
  
  if (arg.X_op != O_add)
    {
      ill_op ();
      return p;
    }
  
  if (!(arg.X_add_number & R_INDEX))
    {
      ill_op ();
      return p;
    }

  q = frag_more (2);
  if (q == NULL)
    {
      return p;
    }
  
  *q++ = prefix;
  *q = opcode;
  
  if (arg.X_add_number == REG_IY)
    {
      *q = opcode + 1;
    }

  arg.X_op = O_symbol;
  arg.X_add_number = 0;
  emit_byte (&arg, BFD_RELOC_Z80_DISP8);

  return p;
}

static const char *
emit_reti (char prefix, char opcode, const char * args)
{
  if ((ins_ok & INS_GBZ80) != 0)
    {
      return emit_insn (0x00, 0xD9, args);
    }

  return emit_insn (prefix, opcode, args);
}

static const char *
emit_tst (char prefix, char opcode, const char *args)
{
  expressionS arg_s;
  const char *p;
  char *q;
  int rnum;

  p = parse_exp (args, &arg_s);
  
  if (*p == ',' && arg_s.X_md == 0 && arg_s.X_op == O_register && arg_s.X_add_number == REG_A)
    {
      if ((ins_ok & INS_EZ80) == 0)
        {
          ill_op ();
          return p;
        }
      ++p;
      p = parse_exp (p, &arg_s);
    }

  if (arg_s.X_op == O_md1)
    {
      ill_op ();
      return p;
    }

  if (arg_s.X_op == O_register)
    {
      rnum = arg_s.X_add_number;
      if (arg_s.X_md != 0)
        {
          if (rnum != REG_HL)
            {
              ill_op ();
              return p;
            }
          rnum = 6;
        }
      q = frag_more (2);
      if (q == NULL)
        return p;
      *q++ = prefix;
      *q = opcode | (rnum << 3);
      return p;
    }

  if (arg_s.X_md)
    {
      ill_op ();
      return p;
    }
    
  q = frag_more (2);
  if (q == NULL)
    return p;
    
  if ((ins_ok & INS_Z80N) != 0)
    {
      *q++ = 0xED;
      *q = 0x27;
    }
  else
    {
      *q++ = prefix;
      *q = opcode | 0x60;
    }
  emit_byte (&arg_s, BFD_RELOC_8);
  
  return p;
}

static const char *
emit_insn_n (char prefix, char opcode, const char *args)
{
  expressionS arg;
  const char *p;
  char *q;

  if (!args)
    {
      ill_op ();
      return NULL;
    }

  p = parse_exp (args, &arg);
  if (!p)
    {
      ill_op ();
      return NULL;
    }

  if (arg.X_md || arg.X_op == O_register || arg.X_op == O_md1)
    {
      ill_op ();
      return p;
    }

  q = frag_more (2);
  if (!q)
    {
      ill_op ();
      return p;
    }

  q[0] = prefix;
  q[1] = opcode;
  
  emit_byte (&arg, BFD_RELOC_8);

  return p;
}

static void
emit_data (int size ATTRIBUTE_UNUSED)
{
  const char *p;
  expressionS exp;

  if (is_it_end_of_statement ())
    {
      demand_empty_rest_of_line ();
      return;
    }

  p = skip_space (input_line_pointer);

  for (;;)
    {
      if (*p == '\"' || *p == '\'')
        {
          p = process_string_literal (p);
        }
      else
        {
          p = process_expression (p, &exp);
          if (p == NULL)
            break;
        }

      if (*p != ',')
        break;
      
      p = skip_space (p + 1);
    }

  input_line_pointer = (char *)p;
}

static const char *
process_string_literal (const char *p)
{
  char quote = *p;
  const char *start = ++p;
  int length = 0;

  while (*p != '\0' && *p != quote)
    {
      p++;
      length++;
    }

  if (length > 0)
    {
      char *dest = frag_more (length);
      memcpy (dest, start, length);
    }

  if (*p == '\0')
    {
      as_warn (_("unterminated string"));
      return p;
    }

  return skip_space (p + 1);
}

static const char *
process_expression (const char *p, expressionS *exp)
{
  p = parse_exp (p, exp);

  if (exp->X_op == O_md1 || exp->X_op == O_register)
    {
      ill_op ();
      return NULL;
    }

  if (exp->X_md)
    {
      as_warn (_("parentheses ignored"));
    }

  emit_byte (exp, BFD_RELOC_8);
  return skip_space (p);
}

static void
z80_cons (int size)
{
  const char *p;
  expressionS exp;

  if (is_it_end_of_statement ())
    {
      demand_empty_rest_of_line ();
      return;
    }

  p = skip_space (input_line_pointer);

  while (1)
    {
      p = parse_exp (p, &exp);
      
      if (exp.X_op == O_md1 || exp.X_op == O_register)
        {
          ill_op ();
          break;
        }
      
      if (exp.X_md)
        as_warn (_("parentheses ignored"));
      
      emit_data_val (&exp, size);
      
      p = skip_space (p);
      
      if (*p != ',')
        break;
      
      p++;
    }
  
  input_line_pointer = (char *)p;
}

/* next functions were commented out because it is difficult to mix
   both ADL and Z80 mode instructions within one COFF file:
   objdump cannot recognize point of mode switching.
*/
static void
set_cpu_mode (int mode)
{
  if ((ins_ok & INS_EZ80) == 0)
    {
      error (_("CPU mode is unsupported by target"));
      return;
    }
  cpu_mode = mode;
}

static void
assume (int arg ATTRIBUTE_UNUSED)
{
  char *name;
  char c;
  int n;

  input_line_pointer = (char*)skip_space (input_line_pointer);
  c = get_symbol_name (&name);
  
  if (strncasecmp (name, "ADL", 3) != 0)
    {
      ill_op ();
      restore_line_pointer (c);
      return;
    }

  restore_line_pointer (c);
  input_line_pointer = (char*)skip_space (input_line_pointer);
  
  if (*input_line_pointer != '=')
    {
      error (_("assignment expected"));
      return;
    }
  
  input_line_pointer++;
  input_line_pointer = (char*)skip_space (input_line_pointer);
  n = get_single_number ();
  set_cpu_mode (n);
}

static const char *
emit_mulub(char prefix ATTRIBUTE_UNUSED, char opcode, const char *args)
{
    const char *p = skip_space(args);
    
    if (TOLOWER(*p) != 'a' || *(p + 1) != ',') {
        ill_op();
        return p;
    }
    
    p += 2;
    char reg = TOLOWER(*p);
    p++;
    
    if (reg < 'b' || reg > 'e') {
        ill_op();
        return p;
    }
    
    check_mach(INS_R800);
    
    if (*skip_space(p) != '\0') {
        ill_op();
        return p;
    }
    
    char *q = frag_more(2);
    *q++ = prefix;
    *q = opcode + ((reg - 'b') << 3);
    
    return p;
}

static const char *
emit_muluw (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  const char *p;
  expressionS reg;
  char *q;

  p = skip_space (args);
  
  if (TOLOWER (*p) != 'h' || TOLOWER (*(p + 1)) != 'l' || *(p + 2) != ',')
    {
      ill_op ();
      return p;
    }
  
  p += 3;
  p = parse_exp (p, &reg);

  if (reg.X_md || reg.X_op != O_register)
    {
      ill_op ();
      return p;
    }

  if (reg.X_add_number != REG_BC && reg.X_add_number != REG_SP)
    {
      ill_op ();
      return p;
    }

  check_mach (INS_R800);
  q = frag_more (2);
  *q++ = prefix;
  *q = opcode + ((reg.X_add_number & 3) << 4);
  
  return p;
}

static int
assemble_suffix (const char **suffix)
{
  static const char sf[8][4] = 
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
  
  const char *p = *suffix;
  if (p == NULL || *p != '.')
    return 0;
  
  p++;
  
  char sbuf[4] = {0};
  int i;
  for (i = 0; i < 3 && ISALPHA(*p); i++)
  {
    sbuf[i] = TOLOWER(*p);
    p++;
  }
  
  if (*p && !is_whitespace(*p))
    return 0;
  
  *suffix = p;
  
  const char (*t)[4] = bsearch(sbuf, sf, ARRAY_SIZE(sf), sizeof(sf[0]), 
                               (int(*)(const void*, const void*))strcmp);
  if (t == NULL)
    return 0;
  
  int index = t - sf;
  int opcode = get_opcode_for_suffix(index);
  
  *frag_more(1) = opcode;
  set_inst_mode_for_opcode(opcode);
  
  return 1;
}

static int get_opcode_for_suffix(int index)
{
  static const int opcodes[8] = {
    -1,   
    -1,   
    -1,   
    0x5B, 
    0x49, 
    -1,   
    0x52, 
    0x40  
  };
  
  if (index < 0 || index >= 8)
    return 0;
  
  int opcode = opcodes[index];
  if (opcode != -1)
    return opcode;
  
  static const int cpu_mode_opcodes[3][2] = {
    {0x52, 0x5B}, 
    {0x40, 0x49}, 
    {0x40, 0x52}  
  };
  
  static const int mode_indices[3] = {0, 1, 5};
  
  for (int i = 0; i < 3; i++)
  {
    if (index == mode_indices[i])
      return cpu_mode ? cpu_mode_opcodes[i][1] : cpu_mode_opcodes[i][0];
  }
  
  return cpu_mode ? 0x49 : 0x5B;
}

static void set_inst_mode_for_opcode(int opcode)
{
  switch (opcode)
  {
    case 0x40:
      inst_mode = INST_MODE_FORCED | INST_MODE_S | INST_MODE_IS;
      break;
    case 0x49:
      inst_mode = INST_MODE_FORCED | INST_MODE_L | INST_MODE_IS;
      break;
    case 0x52:
      inst_mode = INST_MODE_FORCED | INST_MODE_S | INST_MODE_IL;
      break;
    case 0x5B:
      inst_mode = INST_MODE_FORCED | INST_MODE_L | INST_MODE_IL;
      break;
    default:
      break;
  }
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
    {
      as_fatal (_("Invalid directive"));
      return;
    }

  old_ins = ins_ok;
  ins_ok = (ins_ok & INS_MARCH_MASK) | inss;
  
  if (old_ins != ins_ok)
    {
      cpu_mode = 0;
    }
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
  char saved_char;
  
  if (!sdcc_compat)
    {
      as_fatal (_("Invalid directive"));
      return;
    }
    
  p = input_line_pointer;
  while (*p != '\0' && *p != '(' && *p != '\n')
    {
      p++;
    }
    
  if (*p != '(')
    {
      psect (arg);
      return;
    }
    
  saved_char = *p;
  *p = '\n';
  psect (arg);
  *p = saved_char;
  p++;
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

void md_assemble(char *str) {
    const char *p;
    char *old_ptr;
    int i;
    table_t *insp;

    err_flag = 0;
    inst_mode = cpu_mode ? (INST_MODE_L | INST_MODE_IL) : (INST_MODE_S | INST_MODE_IS);
    old_ptr = input_line_pointer;
    p = skip_space(str);
    
    for (i = 0; (i < BUFLEN) && (ISALPHA(*p) || ISDIGIT(*p)); i++) {
        buf[i] = TOLOWER(*p);
        p++;
    }

    if (i == BUFLEN) {
        buf[BUFLEN - 3] = '.';
        buf[BUFLEN - 2] = '.';
        buf[BUFLEN - 1] = '\0';
        as_bad(_("Unknown instruction '%s'"), buf);
        input_line_pointer = old_ptr;
        return;
    }

    dwarf2_emit_insn(0);
    
    if (*p && !is_whitespace(*p)) {
        if (*p != '.' || !(ins_ok & INS_EZ80) || !assemble_suffix(&p)) {
            as_bad(_("syntax error"));
            input_line_pointer = old_ptr;
            return;
        }
    }
    
    buf[i] = '\0';
    p = skip_space(p);
    key = buf;

    insp = bsearch(&key, instab, ARRAY_SIZE(instab), sizeof(instab[0]), key_cmp);
    
    if (!insp || (insp->inss && !(insp->inss & ins_ok))) {
        *frag_more(1) = 0;
        as_bad(_("Unknown instruction `%s'"), buf);
        input_line_pointer = old_ptr;
        return;
    }

    p = insp->fp(insp->prefix, insp->opcode, p);
    p = skip_space(p);
    
    if (!err_flag && *p) {
        as_bad(_("junk at end of line, first unrecognized character is `%c'"), *p);
    }
    
    input_line_pointer = old_ptr;
}

static int signed_overflow(signed long value, unsigned bitsize)
{
    if (bitsize == 0 || bitsize > sizeof(signed long) * 8) {
        return 1;
    }
    
    signed long max = (signed long)((1UL << (bitsize - 1)) - 1);
    signed long min = -max - 1;
    
    return value < min || value > max;
}

static int unsigned_overflow(unsigned long value, unsigned bitsize)
{
    if (bitsize == 0 || bitsize > (sizeof(unsigned long) * 8)) {
        return 1;
    }
    
    if (bitsize >= (sizeof(unsigned long) * 8)) {
        return 0;
    }
    
    unsigned long max_value = ~0UL;
    unsigned int shift_amount = sizeof(unsigned long) * 8 - bitsize;
    max_value = max_value >> shift_amount;
    
    return value > max_value;
}

static int is_overflow(long value, unsigned bitsize)
{
    return (value < 0) ? signed_overflow(value, bitsize) : unsigned_overflow(value, bitsize);
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

  switch (fixP->fx_r_type)
    {
    case BFD_RELOC_8_PCREL:
    case BFD_RELOC_Z80_DISP8:
      if (fixP->fx_done && signed_overflow (val, 8))
	as_bad_where (fixP->fx_file, fixP->fx_line,
		      _("8-bit signed offset out of range (%+ld)"), val);
      *p_lit = val;
      break;

    case BFD_RELOC_Z80_BYTE0:
      *p_lit = val;
      break;

    case BFD_RELOC_Z80_BYTE1:
      *p_lit = (val >> 8);
      break;

    case BFD_RELOC_Z80_BYTE2:
      *p_lit = (val >> 16);
      break;

    case BFD_RELOC_Z80_BYTE3:
      *p_lit = (val >> 24);
      break;

    case BFD_RELOC_8:
      if (fixP->fx_done && is_overflow(val, 8))
	as_warn_where (fixP->fx_file, fixP->fx_line,
		       _("8-bit overflow (%+ld)"), val);
      *p_lit = val;
      break;

    case BFD_RELOC_Z80_WORD1:
      p_lit[0] = (val >> 16);
      p_lit[1] = (val >> 24);
      break;

    case BFD_RELOC_Z80_WORD0:
      p_lit[0] = val;
      p_lit[1] = (val >> 8);
      break;

    case BFD_RELOC_16:
      if (fixP->fx_done && is_overflow(val, 16))
	as_warn_where (fixP->fx_file, fixP->fx_line,
		       _("16-bit overflow (%+ld)"), val);
      p_lit[0] = val;
      p_lit[1] = (val >> 8);
      break;

    case BFD_RELOC_24:
      if (fixP->fx_done && is_overflow(val, 24))
	as_warn_where (fixP->fx_file, fixP->fx_line,
		       _("24-bit overflow (%+ld)"), val);
      p_lit[0] = val;
      p_lit[1] = (val >> 8);
      p_lit[2] = (val >> 16);
      break;

    case BFD_RELOC_32:
      if (fixP->fx_done && is_overflow(val, 32))
	as_warn_where (fixP->fx_file, fixP->fx_line,
		       _("32-bit overflow (%+ld)"), val);
      p_lit[0] = val;
      p_lit[1] = (val >> 8);
      p_lit[2] = (val >> 16);
      p_lit[3] = (val >> 24);
      break;

    case BFD_RELOC_Z80_16_BE:
      p_lit[0] = val >> 8;
      p_lit[1] = val;
      break;

    default:
      as_fatal (_("md_apply_fix: unknown reloc type 0x%x"), fixP->fx_r_type);
      break;
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
tc_gen_reloc (asection *seg ATTRIBUTE_UNUSED, fixS *fixp)
{
  arelent *reloc;
  
  if (fixp == NULL)
    return NULL;
    
  if (fixp->fx_subsy != NULL)
    {
      as_bad_subtract (fixp);
      return NULL;
    }

  reloc = notes_alloc (sizeof (arelent));
  if (reloc == NULL)
    return NULL;
    
  reloc->sym_ptr_ptr = notes_alloc (sizeof (asymbol *));
  if (reloc->sym_ptr_ptr == NULL)
    return NULL;
    
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

  if (fixp->fx_r_type == BFD_RELOC_VTABLE_INHERIT ||
      fixp->fx_r_type == BFD_RELOC_VTABLE_ENTRY)
    {
      reloc->address = fixp->fx_offset;
    }

  return reloc;
}

int z80_tc_labels_without_colon(void)
{
    return colonless_labels;
}

int z80_tc_label_is_local(const char *name)
{
  if (local_label_prefix == NULL || name == NULL)
    return 0;
  
  size_t prefix_len = strlen(local_label_prefix);
  if (prefix_len == 0)
    return 0;
  
  return strncmp(name, local_label_prefix, prefix_len) == 0 &&
         local_label_prefix[prefix_len - 1] != '\0';
}

/* Parse floating point number from string and compute mantissa and
   exponent. Mantissa is normalized.
*/
#define EXP_MIN -0x10000
#define EXP_MAX 0x10000
static int
str_to_broken_float (bool *signP, uint64_t *mantissaP, int *expP)
{
  char *p;
  bool sign;
  uint64_t mantissa = 0;
  int exponent = 0;
  int binary_exp = 64;

  if (!signP || !mantissaP || !expP)
    return 0;

  p = (char*)skip_space (input_line_pointer);
  if (!p)
    return 0;

  sign = (*p == '-');
  *signP = sign;
  if (sign || *p == '+')
    ++p;

  if (strncasecmp (p, "NaN", 3) == 0)
    {
      *mantissaP = 0;
      *expP = 0;
      input_line_pointer = p + 3;
      return 1;
    }

  if (strncasecmp (p, "inf", 3) == 0)
    {
      *mantissaP = 1ull << 63;
      *expP = EXP_MAX;
      input_line_pointer = p + 3;
      return 1;
    }

  while (ISDIGIT (*p))
    {
      if (mantissa > (UINT64_MAX / 10))
        {
          if (*p >= '5')
            mantissa++;
          while (ISDIGIT (*p))
            {
              exponent++;
              p++;
            }
          break;
        }
      mantissa = mantissa * 10 + (*p - '0');
      p++;
    }

  if (*p == '.')
    {
      p++;
      while (ISDIGIT (*p))
        {
          if (exponent == 0 && mantissa <= (UINT64_MAX / 10))
            {
              mantissa = mantissa * 10 + (*p - '0');
              exponent--;
            }
          p++;
        }
    }

  if (*p == 'e' || *p == 'E')
    {
      int exp_sign = 0;
      int exp_value = 0;
      p++;
      
      if (*p == '-')
        {
          exp_sign = 1;
          p++;
        }
      else if (*p == '+')
        {
          p++;
        }

      while (ISDIGIT (*p))
        {
          if (exp_value <= (INT_MAX / 10))
            exp_value = exp_value * 10 + (*p - '0');
          p++;
        }
      
      if (exp_sign)
        exponent -= exp_value;
      else
        exponent += exp_value;
    }

  if (ISALNUM (*p) || *p == '.')
    return 0;

  input_line_pointer = p;

  if (mantissa == 0)
    {
      *mantissaP = 1ull << 63;
      *expP = EXP_MIN;
      return 1;
    }

  while (mantissa <= (UINT64_MAX / 10))
    {
      mantissa *= 10;
      exponent--;
    }

  while (exponent > 0)
    {
      while (mantissa > (UINT64_MAX / 10))
        {
          mantissa >>= 1;
          binary_exp++;
        }
      mantissa *= 10;
      exponent--;
    }

  while (exponent < 0)
    {
      while ((mantissa & (1ull << 63)) == 0)
        {
          mantissa <<= 1;
          binary_exp--;
        }
      mantissa /= 10;
      exponent++;
    }

  while ((mantissa & (1ull << 63)) == 0)
    {
      mantissa <<= 1;
      binary_exp--;
    }

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
  unsigned i;

  if (litP == NULL || sizeP == NULL)
    return _("invalid parameters");

  *sizeP = 4;
  
  if (!str_to_broken_float(&sign, &mantissa, &exponent))
    return _("invalid syntax");
  
  exponent--;
  mantissa >>= 39;
  mantissa++;
  mantissa >>= 1;
  
  if (mantissa >> 24)
    {
      mantissa >>= 1;
      exponent++;
    }
  
  if (exponent < -127)
    {
      exponent = -128;
      mantissa = 0;
    }
  else if (exponent > 127)
    {
      exponent = -128;
      mantissa = sign ? 0xc00000 : 0x400000;
    }
  else if (mantissa == 0)
    {
      exponent = -128;
      mantissa = 0x200000;
    }
  else if (!sign)
    {
      mantissa &= (1ull << 23) - 1;
    }
  
  for (i = 0; i < 24; i += 8)
    {
      *litP++ = (char)(mantissa >> i);
    }
  
  *litP = (char)(0x80 + exponent);
  
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
static const char *
str_to_float48(char *litP, int *sizeP)
{
  uint64_t mantissa;
  bool sign;
  int exponent;
  unsigned i;

  *sizeP = 6;
  
  if (!str_to_broken_float(&sign, &mantissa, &exponent))
    return _("invalid syntax");
  
  mantissa >>= 23;
  mantissa++;
  mantissa >>= 1;
  
  if (mantissa >> 40)
    {
      mantissa >>= 1;
      exponent++;
    }
  
  if (exponent < -127)
    {
      memset(litP, 0, 6);
      return NULL;
    }
  
  if (exponent > 127)
    return _("overflow");
  
  if (!sign)
    mantissa &= (1ull << 39) - 1;
  
  *litP++ = 0x80 + exponent;
  
  for (i = 0; i < 40; i += 8)
    *litP++ = mantissa >> i;
  
  return NULL;
}

static const char *
str_to_ieee754_h(char *litP, int *sizeP)
{
  if (litP == NULL || sizeP == NULL) {
    return NULL;
  }
  return ieee_md_atof('h', litP, sizeP, false);
}

static const char *
str_to_ieee754_s(char *litP, int *sizeP)
{
  if (litP == NULL || sizeP == NULL) {
    return NULL;
  }
  return ieee_md_atof('s', litP, sizeP, false);
}

static const char *
str_to_ieee754_d(char *litP, int *sizeP)
{
  if (litP == NULL || sizeP == NULL)
    return NULL;
    
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
  if (abfd == NULL)
    return 2;
    
  return (bfd_get_mach (abfd) == bfd_mach_ez80_adl) ? 3 : 2;
}
