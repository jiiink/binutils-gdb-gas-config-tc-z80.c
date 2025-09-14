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

static int cpu_match_and_set(const char *key, size_t key_len, int *ok, int *err, int *mode)
{
  size_t i;
  for (i = 0; i < ARRAY_SIZE(match_cpu_table); ++i)
    {
      const char *ename = match_cpu_table[i].name;
      if (strlen(ename) == key_len && strncasecmp(key, ename, key_len) == 0)
        {
          *ok = match_cpu_table[i].ins_ok;
          *err = match_cpu_table[i].ins_err;
          *mode = match_cpu_table[i].cpu_mode;
          return 1;
        }
    }
  return 0;
}

static int find_extension_index(const char *key, size_t key_len)
{
  size_t i;
  for (i = 0; i < ARRAY_SIZE(match_ext_table); ++i)
    {
      const char *ename = match_ext_table[i].name;
      if (strlen(ename) == key_len && strncasecmp(key, ename, key_len) == 0)
        return (int)i;
    }
  return -1;
}

static void
setup_march (const char *name, int *ok, int *err, int *mode)
{
  const char *p;
  size_t len;

  if (name == NULL || ok == NULL || err == NULL || mode == NULL)
    {
      as_fatal (_("Invalid arguments to setup_march"));
      return;
    }

  p = name;
  len = strcspn (p, "+-");

  if (!cpu_match_and_set(p, len, ok, err, mode))
    as_fatal (_("Invalid CPU is specified: %s"), name);

  while (p[len] != '\0')
    {
      char sign = p[len];
      int idx;

      p += len + 1;
      len = strcspn (p, "+-");

      idx = find_extension_index(p, len);
      if (idx < 0)
        as_fatal (_("Invalid EXTENSION is specified: %s"), p);

      if (sign == '+')
        {
          *ok |= match_ext_table[idx].ins_ok;
          *err &= ~match_ext_table[idx].ins_ok;
          *mode |= match_ext_table[idx].cpu_mode;
        }
      else
        {
          *ok &= ~match_ext_table[idx].ins_ok;
          *err |= match_ext_table[idx].ins_ok;
          *mode &= ~match_ext_table[idx].cpu_mode;
        }
    }
}

static int setup_instruction(const char *inst, int *add, int *sub)
{
    if (inst == NULL || add == NULL || sub == NULL)
        return 0;

    static const struct {
        const char *name;
        int value;
    } map[] = {
        { "idx-reg-halves", INS_IDX_HALF },
        { "sli",            INS_SLI },
        { "op-ii-ld",       INS_ROT_II_LD },
        { "in-f-c",         INS_IN_F_C },
        { "out-c-0",        INS_OUT_C_0 }
    };

    int n = 0;
    int i;
    int count = (int)(sizeof(map) / sizeof(map[0]));
    for (i = 0; i < count; i++) {
        if (strcmp(inst, map[i].name) == 0) {
            n = map[i].value;
            break;
        }
    }
    if (i == count)
        return 0;

    *add |= n;
    *sub &= ~n;
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
get_str_to_float(const char *arg)
{
  if (arg == NULL)
    {
      as_fatal(_("invalid floating point numbers type `%s'"), "(null)");
      return NULL;
    }

  if (strcasecmp(arg, "zeda32") == 0)
    return str_to_zeda32;

  if (strcasecmp(arg, "math48") == 0)
    return str_to_float48;

  if (strcasecmp(arg, "half") == 0)
    return str_to_ieee754_h;

  if (strcasecmp(arg, "single") == 0)
    return str_to_ieee754_s;

  if (strcasecmp(arg, "double") == 0)
    return str_to_ieee754_d;

  if (strcasecmp(arg, "ieee754") == 0)
    {
      as_fatal(_("invalid floating point numbers type `%s'"), arg);
      return NULL;
    }

  return NULL;
}

static int
setup_instruction_list (const char *list, int *add, int *sub)
{
  if (list == NULL)
    {
      as_bad (_("invalid INST in command line: (null)"));
      return 0;
    }

  char buf[16];
  const char *b = list;
  int res = 0;

  while (*b != '\0')
    {
      const char *e = strchr (b, ',');
      size_t sz = (e == NULL) ? strlen (b) : (size_t) (e - b);

      if (sz == 0 || sz >= sizeof (buf))
        {
          as_bad (_("invalid INST in command line: %s"), b);
          return 0;
        }

      memcpy (buf, b, sz);
      buf[sz] = '\0';

      if (!setup_instruction (buf, add, sub))
        {
          as_bad (_("invalid INST in command line: %s"), buf);
          return 0;
        }

      res++;

      b += sz;
      if (*b == ',')
        b++;
    }

  return res;
}

static int is_gbz80_active(void)
{
  return (ins_ok & INS_GBZ80) != 0;
}

static const char* march_from_option(int c)
{
  switch (c)
    {
    case OPTION_MACH_Z80:       return "z80";
    case OPTION_MACH_R800:      return "r800";
    case OPTION_MACH_Z180:      return "z180";
    case OPTION_MACH_EZ80_Z80:  return "ez80";
    case OPTION_MACH_EZ80_ADL:  return "ez80+adl";
    default:                    return NULL;
    }
}

int
md_parse_option (int c, const char* arg)
{
  const char* arch = march_from_option(c);
  if (arch != NULL)
    {
      setup_march (arch, &ins_ok, &ins_err, &cpu_mode);
      return 1;
    }

  switch (c)
    {
    case OPTION_MARCH:
      setup_march (arg, &ins_ok, &ins_err, &cpu_mode);
      break;

    case OPTION_FP_SINGLE_FORMAT:
      str_to_float = get_str_to_float (arg);
      break;

    case OPTION_FP_DOUBLE_FORMAT:
      str_to_double = get_str_to_float (arg);
      break;

    case OPTION_MACH_INST:
      if (!is_gbz80_active())
        return setup_instruction_list (arg, &ins_ok, &ins_err);
      break;

    case OPTION_MACH_NO_INST:
      if (!is_gbz80_active())
        return setup_instruction_list (arg, &ins_err, &ins_ok);
      break;

    case OPTION_MACH_WUD:
    case OPTION_MACH_IUD:
      if (!is_gbz80_active())
        {
          ins_ok |= INS_UNDOC;
          ins_err &= ~INS_UNDOC;
        }
      break;

    case OPTION_MACH_WUP:
    case OPTION_MACH_IUP:
      if (!is_gbz80_active())
        {
          ins_ok |= (INS_UNDOC | INS_UNPORT);
          ins_err &= ~(INS_UNDOC | INS_UNPORT);
        }
      break;

    case OPTION_MACH_FUD:
      if ((ins_ok & (INS_R800 | INS_GBZ80)) == 0)
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

    default:
      return 0;
    }

  return 1;
}

void md_show_usage(FILE *f)
{
  if (f == NULL)
    return;

  const char *header1 = _("\nCPU model options:\n  -march=CPU[+EXT...][-EXT...]\n\t\t\t  generate code for CPU, where CPU is one of:\n");
  if (fputs(header1, f) == EOF)
    return;

  size_t cpu_count = ARRAY_SIZE(match_cpu_table);
  for (size_t i = 0; i < cpu_count; ++i)
    {
      const char *name = match_cpu_table[i].name ? match_cpu_table[i].name : "";
      const char *comment = match_cpu_table[i].comment ? match_cpu_table[i].comment : "";
      if (fprintf(f, "  %-8s\t\t  %s\n", name, comment) < 0)
        return;
    }

  const char *header2 = _("And EXT is combination (+EXT - add, -EXT - remove) of:\n");
  if (fputs(header2, f) == EOF)
    return;

  size_t ext_count = ARRAY_SIZE(match_ext_table);
  for (size_t i = 0; i < ext_count; ++i)
    {
      const char *name = match_ext_table[i].name ? match_ext_table[i].name : "";
      const char *comment = match_ext_table[i].comment ? match_ext_table[i].comment : "";
      if (fprintf(f, "  %-8s\t\t  %s\n", name, comment) < 0)
        return;
    }

  const char *footer =
    _("\nCompatibility options:\n"
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
      "Default: -march=z80+xyhl+infc\n");
  (void) fputs(footer, f);
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
  char *saved_input;
  unsigned int i;
  char buf[BUFLEN];

  memset(&reg, 0, sizeof(reg));
  memset(&nul, 0, sizeof(nul));

  if (ins_ok & INS_EZ80)
    listing_lhs_width = 6;

  reg.X_op = O_register;
  reg.X_md = 0;
  reg.X_add_symbol = 0;
  reg.X_op_symbol = 0;

  for (i = 0; i < ARRAY_SIZE(regtable); ++i)
    {
      const char *name = regtable[i].name;
      size_t name_len;
      unsigned int variations, v;

      if (regtable[i].isa && !(regtable[i].isa & ins_ok))
        continue;

      if (!name)
        continue;

      name_len = strlen(name);

      if (name_len + 1 < BUFLEN)
        {
          if (name_len >= (sizeof(unsigned int) * 8u))
            continue;

          reg.X_add_number = regtable[i].number;
          variations = 1U << name_len;

          for (v = 1; v <= variations; ++v)
            {
              size_t idx;
              for (idx = 0; idx < name_len; ++idx)
                {
                  int ch = name[idx];
                  buf[idx] = (v & (1U << idx)) ? TOUPPER(ch) : ch;
                }
              buf[name_len] = '\0';

              {
                symbolS *psym = symbol_find_or_make(buf);
                if (psym)
                  {
                    S_SET_SEGMENT(psym, reg_section);
                    symbol_set_value_expression(psym, &reg);
                  }
              }
            }
        }
    }

  saved_input = input_line_pointer;
  input_line_pointer = (char *) "0";
  nul.X_md = 0;
  expression(&nul);
  input_line_pointer = saved_input;
  zero = make_expr_symbol(&nul);
  linkrelax = 0;
}

static unsigned long
z80_determine_mach_type (unsigned long march)
{
  switch (march)
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
      return 0UL;
    }
}

void
z80_md_finish (void)
{
  unsigned long mach_type = z80_determine_mach_type ((unsigned long) (ins_ok & INS_MARCH_MASK));
  if (stdoutput == NULL)
    return;
  (void) bfd_set_arch_mach (stdoutput, TARGET_ARCH, mach_type);
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
        s++;
    }
    return s;
}

/* A non-zero return-value causes a continue in the
   function read_a_source_file () in ../read.c.  */
int
z80_start_line_hook (void)
{
  char *p, quote;
  char buf[4];

  for (p = input_line_pointer; *p && *p != '\n'; ++p)
    {
      if (*p == '\'' || *p == '"')
        {
          if (*p == '\'' && p[1] != 0 && p[1] != '\'' && p[2] == '\'')
            {
              snprintf (buf, sizeof (buf), "%3d", (unsigned char) p[1]);
              *p++ = buf[0];
              *p++ = buf[1];
              *p++ = buf[2];
              continue;
            }
          quote = *p++;
          while (*p != quote && *p != '\n' && *p != '\0')
            ++p;
          if (*p != quote)
            {
              as_bad (_("-- unterminated string"));
              ignore_rest_of_line ();
              return 1;
            }
          continue;
        }
      if (*p == '#')
        {
          if (!sdcc_compat)
            continue;
          if (is_whitespace (p[1]) && *skip_space (p + 1) == '(')
            {
              *p++ = '0';
              *p = '+';
            }
          else
            {
              *p = (p[1] == '(') ? '+' : ' ';
            }
          continue;
        }
    }

  if (sdcc_compat && *input_line_pointer == '0')
    {
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

  if (is_name_beginner (*input_line_pointer))
    {
      char *name;
      char c, *rest, *line_start;
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
      rest = (char *) skip_space (rest);
      if (*rest == '=')
        len = (rest[1] == '=') ? 2 : 1;
      else
        {
          if (*rest == '.')
            ++rest;
          if (strncasecmp (rest, "EQU", 3) == 0)
            len = 3;
          else if (strncasecmp (rest, "DEFL", 4) == 0)
            len = 4;
          else
            len = 0;
        }
      if (len && (len <= 2 || !ISALPHA (rest[len])))
        {
          if (line_start[-1] == '\n')
            {
              bump_line_counters ();
              LISTING_NEWLINE ();
            }
          input_line_pointer = rest + len - 1;
          switch (len)
            {
            case 1:
            case 4:
              equals (name, 1);
              break;
            case 2:
            case 3:
              equals (name, 0);
              break;
            }
          return 1;
        }
      else
        {
          (void) restore_line_pointer (c);
          input_line_pointer = line_start;
        }
    }
  return 0;
}

symbolS *
md_undefined_symbol(char *name)
{
  (void)name;
  return NULL;
}

const char *
md_atof (int type, char *litP, int *sizeP)
{
  const char *(*converter) (char *, int *) = NULL;

  switch (type)
    {
    case 'f':
    case 'F':
    case 's':
    case 'S':
      converter = str_to_float;
      break;
    case 'd':
    case 'D':
    case 'r':
    case 'R':
      converter = str_to_double;
      break;
    default:
      break;
    }

  if (converter)
    return converter (litP, sizeP);

  return ieee_md_atof (type, litP, sizeP, false);
}

valueT
md_section_align(segT seg ATTRIBUTE_UNUSED, valueT size)
{
  (void) seg;
  return size;
}

#include <assert.h>

long
md_pcrel_from(fixS *fixp)
{
    assert(fixp != NULL);
    assert(fixp->fx_frag != NULL);

    long where = fixp->fx_where;
    long address = fixp->fx_frag->fr_address;
    return where + address;
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
    const char * const *pa = (const char * const *)a;
    const char * const *pb = (const char * const *)b;

    if (pa == NULL || pb == NULL) {
        return (pa > pb) - (pa < pb);
    }

    const char *sa = *pa;
    const char *sb = *pb;

    if (sa == NULL || sb == NULL) {
        return (sa > sb) - (sa < sb);
    }

    return strcmp(sa, sb);
}

char buf[BUFLEN];
const char *key = buf;

/* Prevent an error on a line from also generating
   a "junk at end of line" error message.  */
static char err_flag;

static void
error(const char *message)
{
  const char *safe_message;

  if (err_flag) {
    return;
  }

  safe_message = (message != NULL) ? message : "Unknown error";

  as_bad("%s", safe_message);
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
      return;
    }

  as_warn (_("undocumented instruction"));
}

static void check_mach(int ins_type)
{
    if ((ins_type & ins_ok) != 0) {
        return;
    }
    wrong_mach(ins_type);
}

/* Check whether an expression is indirect.  */
static const char *
skip_quoted(const char *p)
{
  char quote = *p;

  if (quote != '"' && quote != '\'')
    return p;

  ++p; /* Skip opening quote */

  while (*p && *p != '\n')
    {
      if (*p == '\\')
        {
          if (p[1] == '\0')
            {
              ++p; /* Move to NUL and stop */
              break;
            }
          p += 2; /* Skip escaped character */
          continue;
        }
      if (*p == quote)
        break;
      ++p;
    }

  return p;
}

static int
is_indir (const char *s)
{
  const char *p;
  int indir, depth;

  if (s == NULL)
    return 0;

  indir = (*s == '(');
  depth = 0;

  for (p = s; *p && *p != ','; ++p)
    {
      switch (*p)
        {
        case '"':
        case '\'':
          p = skip_quoted(p);
          break;

        case '(':
          ++depth;
          break;

        case ')':
          --depth;
          if (depth == 0)
            {
              const char *after = skip_space(p + 1);
              if (*after && *after != ',')
                indir = 0;
              p = after - 1;
            }
          if (depth < 0)
            error (_("mismatched parentheses"));
          break;

        default:
          break;
        }
    }

  if (depth != 0)
    error (_("mismatched parentheses"));

  return indir;
}

/* Check whether a symbol involves a register.  */
static bool
contains_register(symbolS *sym)
{
  if (sym == NULL)
    return false;

  expressionS *ex = symbol_get_value_expression(sym);
  if (ex == NULL)
    return false;

  switch (ex->X_op)
    {
    case O_register:
      return true;

    case O_add:
    case O_subtract:
      if (ex->X_op_symbol && contains_register(ex->X_op_symbol))
        return true;
      if (ex->X_add_symbol && contains_register(ex->X_add_symbol))
        return true;
      return false;

    case O_uminus:
    case O_symbol:
      return ex->X_add_symbol && contains_register(ex->X_add_symbol);

    default:
      return false;
    }
}

/* Parse general expression, not looking for indexed addressing.  */
static const char *
parse_exp_not_indexed (const char *s, expressionS *op)
{
  const char *p = skip_space (s);
  int indir = 0;
  int shift_amount = -1;

  memset (op, 0, sizeof (*op));

  if (sdcc_compat)
    {
      if (*p == '<')
        shift_amount = 0;
      else if (*p == '>')
        shift_amount = cpu_mode ? 16 : 8;

      if (shift_amount >= 0)
        {
          s = p + 1;
          p = skip_space (p + 1);
        }
    }

  indir = (shift_amount == -1) ? is_indir (p) : 0;
  op->X_md = indir;

  if (indir && (ins_ok & INS_GBZ80))
    {
      const char *q = skip_space (p + 1);
      if (!strncasecmp (q, "hl", 2))
        {
          const char *r = skip_space (q + 2);
          if ((*r == '+' || *r == '-') && *skip_space (r + 1) == ')')
            {
              op->X_op = O_md1;
              op->X_add_symbol = NULL;
              op->X_add_number = (*r == '+') ? REG_HL : -REG_HL;
              input_line_pointer = (char *) skip_space (r + 1) + 1;
              return input_line_pointer;
            }
        }
    }

  input_line_pointer = (char *) s;
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
      expressionS data;
      op->X_add_symbol = make_expr_symbol (op);
      op->X_add_number = 0;
      op->X_op = O_right_shift;

      memset (&data, 0, sizeof (data));
      data.X_op = O_constant;
      data.X_add_number = shift_amount;
      op->X_op_symbol = make_expr_symbol (&data);
    }

  return input_line_pointer;
}

static int
unify_indexed (expressionS *op)
{
  if (op == NULL || op->X_add_symbol == NULL)
    return 0;

  expressionS *sym_expr = symbol_get_value_expression (op->X_add_symbol);
  if (sym_expr == NULL || sym_expr->X_op != O_register)
    return 0;

  int rnum = sym_expr->X_add_number;
  int invalid_reg = (rnum != REG_IX && rnum != REG_IY);
  int has_inner_reg = (op->X_op_symbol != NULL) && contains_register (op->X_op_symbol);

  if (invalid_reg || has_inner_reg)
    {
      ill_op ();
      return 0;
    }

  if (op->X_op == O_subtract)
    {
      expressionS minus;
      memset (&minus, 0, sizeof (minus));
      minus.X_op = O_uminus;
      minus.X_add_symbol = op->X_op_symbol;
      op->X_op_symbol = make_expr_symbol (&minus);
      op->X_op = O_add;
    }

  if (op->X_add_number != 0)
    {
      expressionS add;
      memset (&add, 0, sizeof (add));
      add.X_op = O_symbol;
      add.X_add_number = op->X_add_number;
      add.X_add_symbol = op->X_op_symbol;
      op->X_add_symbol = make_expr_symbol (&add);
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
parse_exp(const char *s, expressionS *op)
{
  const char *res = parse_exp_not_indexed(s, op);

  switch (op->X_op)
    {
    case O_add:
    case O_subtract:
      if (unify_indexed(op) && op->X_md)
        op->X_op = O_md1;
      break;

    case O_register:
      {
        int is_ix_or_iy = (op->X_add_number == REG_IX) || (op->X_add_number == REG_IY);
        if (op->X_md && is_ix_or_iy)
          {
            op->X_add_symbol = zero;
            op->X_op = O_md1;
          }
      }
      break;

    case O_constant:
      if (sdcc_compat && is_indir(res))
        {
          expressionS off = *op;
          res = parse_exp(res, op);
          if (op->X_op == O_md1 && op->X_add_symbol == zero)
            op->X_add_symbol = make_expr_symbol(&off);
          else
            ill_op();
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
parse_cc(const char *s, char *op)
{
  if (s == NULL || op == NULL)
    return NULL;

  int i;
  for (i = 0; i < BUFLEN; ++i)
    {
      unsigned char ch = (unsigned char)s[i];
      if (!ISALPHA(ch))
        break;
      buf[i] = TOLOWER(ch);
    }

  if (i >= BUFLEN)
    return NULL;

  unsigned char term = (unsigned char)s[i];
  if (term != '\0' && term != ',')
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
emit_insn(char prefix, char opcode, const char *args)
{
  const int len = prefix ? 2 : 1;
  char *p = frag_more(len);

  if (p == NULL)
    return args;

  if (prefix)
    *p++ = (unsigned char) prefix;

  *p = (unsigned char) opcode;
  return args;
}

void z80_cons_fix_new(fragS *frag_p, int offset, int nbytes, expressionS *exp)
{
  bfd_reloc_code_real_type reloc;

  switch (nbytes)
    {
    case 1: reloc = BFD_RELOC_8; break;
    case 2: reloc = BFD_RELOC_16; break;
    case 3: reloc = BFD_RELOC_24; break;
    case 4: reloc = BFD_RELOC_32; break;
    default:
      as_bad(_("unsupported BFD relocation size %u"), (unsigned) nbytes);
      return;
    }

  fix_new_exp(frag_p, offset, nbytes, exp, 0, reloc);
}

static void
emit_data_val (expressionS *val, int size)
{
  char *p;
  bfd_reloc_code_real_type r_type;

  if (val == NULL)
    {
      as_fatal (_("null expression"));
      return;
    }

  if (size < 0)
    {
      as_fatal (_("invalid data size %d"), size);
      return;
    }

  p = frag_more (size);
  if (p == NULL)
    {
      as_fatal (_("out of memory"));
      return;
    }

  if (val->X_op == O_constant)
    {
      int i;
      unsigned int bits;

      if (size > (INT_MAX / 8))
        {
          as_fatal (_("invalid data size %d"), size);
          return;
        }

      bits = (unsigned int) (size * 8);

      if (!val->X_extrabit && is_overflow (val->X_add_number, bits))
        as_warn (_("%d-bit overflow (%+" PRId64 ")"), bits,
                 (int64_t) val->X_add_number);

      {
        uint64_t v = (uint64_t) val->X_add_number;
        for (i = 0; i < size; ++i)
          p[i] = (char) ((v >> (i * 8)) & 0xffu);
      }
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
      return;
    }

  if ((val->X_op == O_register)
      || (val->X_op == O_md1)
      || contains_register (val->X_add_symbol)
      || contains_register (val->X_op_symbol))
    ill_op ();

  if (size <= 2 && val->X_op_symbol)
    {
      bool simplify = true;
      int shift = symbol_get_value_expression (val->X_op_symbol)->X_add_number;

      if (val->X_op == O_bit_and && shift == (1 << (size * 8)) - 1)
        shift = 0;
      else if (val->X_op != O_right_shift)
        shift = -1;

      if (size == 1)
        {
          switch (shift)
            {
            case 0:  r_type = BFD_RELOC_Z80_BYTE0; break;
            case 8:  r_type = BFD_RELOC_Z80_BYTE1; break;
            case 16: r_type = BFD_RELOC_Z80_BYTE2; break;
            case 24: r_type = BFD_RELOC_Z80_BYTE3; break;
            default: simplify = false; break;
            }
        }
      else
        {
          switch (shift)
            {
            case 0:  r_type = BFD_RELOC_Z80_WORD0; break;
            case 16: r_type = BFD_RELOC_Z80_WORD1; break;
            case 8:
            case 24:
              val->X_op = O_symbol;
              val->X_op_symbol = NULL;
              val->X_add_number = 0;
              if (shift == 8)
                {
                  fix_new_exp (frag_now, p++ - frag_now->fr_literal, 1, val, false,
                               BFD_RELOC_Z80_BYTE1);
                  r_type = BFD_RELOC_Z80_BYTE2;
                }
              else
                r_type = BFD_RELOC_Z80_BYTE3;
              size = 1;
              simplify = false;
              break;
            default:
              simplify = false; break;
            }
        }

      if (simplify)
        {
          val->X_op = O_symbol;
          val->X_op_symbol = NULL;
          val->X_add_number = 0;
        }
    }

  fix_new_exp (frag_now, p - frag_now->fr_literal, size, val, false, r_type);
}

static void
emit_byte(expressionS *val, bfd_reloc_code_real_type r_type)
{
  if (val == NULL)
    {
      as_bad(_("internal error: null expression"));
      return;
    }

  if (r_type == BFD_RELOC_8)
    {
      emit_data_val(val, 1);
      return;
    }

  char *p = frag_more(1);
  if (p == NULL)
    {
      as_bad(_("internal error: failed to allocate fragment"));
      return;
    }

  int64_t num = (int64_t) val->X_add_number;
  *p = (char) num;

  if (contains_register(val->X_add_symbol) || contains_register(val->X_op_symbol))
    {
      ill_op();
      return;
    }

  int is_pcrel_8 = (r_type == BFD_RELOC_8_PCREL);
  if (is_pcrel_8 && (val->X_op == O_constant))
    {
      as_bad(_("cannot make a relative jump to an absolute location"));
      return;
    }

  if (val->X_op == O_constant)
    {
      if (num < -128 || num >= 128)
        {
          if (r_type == BFD_RELOC_Z80_DISP8)
            as_bad(_("index overflow (%+" PRId64 ")"), num);
          else
            as_bad(_("offset overflow (%+" PRId64 ")"), num);
        }
      return;
    }

  fix_new_exp(frag_now, p - frag_now->fr_literal, 1, val, is_pcrel_8, r_type);
}

static void emit_word(expressionS *val)
{
    const unsigned int width = ((inst_mode & INST_MODE_IL) != 0U) ? 3U : 2U;
    emit_data_val(val, width);
}

static void
emit_mx (char prefix, char opcode, int shift, expressionS *arg)
{
  char *q;
  int rnum = arg->X_add_number;
  unsigned char prefixByte = (unsigned char) prefix;
  unsigned char opcodeByte = (unsigned char) opcode;
  const int memRegCode = 6;

  switch (arg->X_op)
    {
    case O_register:
      if (arg->X_md)
        {
          if (rnum != REG_HL)
            {
              ill_op();
              return;
            }
          rnum = memRegCode;
        }
      else
        {
          if ((prefixByte == 0) && (rnum & R_INDEX))
            {
              prefixByte = (unsigned char) ((rnum & R_IX) ? 0xDD : 0xFD);
              if (!(ins_ok & (INS_EZ80 | INS_R800 | INS_Z80N)))
                check_mach (INS_IDX_HALF);
              rnum &= ~R_INDEX;
            }
          if (rnum > 7)
            {
              ill_op();
              return;
            }
        }
      q = frag_more(prefixByte ? 2 : 1);
      if (prefixByte)
        *q++ = (char) prefixByte;
      *q++ = (char) (opcodeByte + (unsigned) ((unsigned) rnum << shift));
      return;

    case O_md1:
      if (ins_ok & INS_GBZ80)
        {
          ill_op();
          return;
        }
      {
        unsigned char indexPrefix = (unsigned char) ((rnum & R_IX) ? 0xDD : 0xFD);
        unsigned char baseOp = (unsigned char) (opcodeByte + (unsigned) ((unsigned) memRegCode << shift));
        q = frag_more(2);
        *q++ = (char) indexPrefix;
        *q = (char) (prefixByte ? prefixByte : baseOp);

        {
          expressionS offset = *arg;
          offset.X_op = O_symbol;
          offset.X_add_number = 0;
          emit_byte(&offset, BFD_RELOC_Z80_DISP8);
        }

        if (prefixByte)
          {
            q = frag_more(1);
            *q = (char) baseOp;
          }
      }
      return;

    default:
      abort();
    }
}

/* The operand m may be r, (hl), (ix+d), (iy+d),
   if 0 = prefix m may also be ixl, ixh, iyl, iyh.  */
static const char *
emit_m(char prefix, char opcode, const char *args)
{
    if (args == NULL) {
        ill_op();
        return NULL;
    }

    expressionS expr = (expressionS){0};
    const char *p = parse_exp(args, &expr);
    if (p == NULL) {
        ill_op();
        return NULL;
    }

    if (expr.X_op == O_md1 || expr.X_op == O_register) {
        emit_mx(prefix, opcode, 0, &expr);
    } else {
        ill_op();
    }

    return p;
}

/* The operand m may be as above or one of the undocumented
   combinations (ix+d),r and (iy+d),r (if unportable instructions
   are allowed).  */

static const char *
emit_mr(char prefix, char opcode, const char *args)
{
  expressionS arg_m, arg_r;
  const char *p;
  int op = opcode;

  if (args == NULL)
    {
      ill_op();
      return NULL;
    }

  p = parse_exp(args, &arg_m);
  if (p == NULL)
    {
      ill_op();
      return NULL;
    }

  switch (arg_m.X_op)
    {
    case O_md1:
      if (*p == ',')
        {
          const char *next = parse_exp(p + 1, &arg_r);
          if (next == NULL)
            {
              ill_op();
              return p;
            }
          p = next;

          if (arg_r.X_md == 0
              && arg_r.X_op == O_register
              && arg_r.X_add_number < 8)
            {
              op += arg_r.X_add_number - 6;
              if (!(ins_ok & INS_Z80N))
                check_mach(INS_ROT_II_LD);
            }
          else
            {
              ill_op();
              return p;
            }
        }
      emit_mx(prefix, (char) op, 0, &arg_m);
      break;

    case O_register:
      emit_mx(prefix, (char) op, 0, &arg_m);
      break;

    default:
      ill_op();
      break;
    }

  return p;
}

static void
emit_sx (char prefix, char opcode, expressionS * arg_p)
{
  if (!arg_p)
    {
      ill_op ();
      return;
    }

  switch (arg_p->X_op)
    {
    case O_register:
    case O_md1:
      emit_mx (prefix, opcode, 0, arg_p);
      return;
    default:
      break;
    }

  if (arg_p->X_md)
    {
      ill_op ();
      return;
    }

  {
    const char xor_mask = 0x46;
    const unsigned int bytes_needed = prefix ? 2u : 1u;
    char *q = frag_more (bytes_needed);
    if (!q)
      {
        ill_op ();
        return;
      }
    if (prefix)
      *q++ = prefix;
    *q = (char)(opcode ^ xor_mask);
    emit_byte (arg_p, BFD_RELOC_8);
  }
}

/* The operand s may be r, (hl), (ix+d), (iy+d), n.  */
static const char *
emit_s(char prefix, char opcode, const char *args)
{
  expressionS arg_s = {0};
  const char *p;

  if (args == NULL)
    {
      ill_op();
      return NULL;
    }

  p = parse_exp(args, &arg_s);
  if (p != NULL)
    {
      int is_generic =
        (*p == ',') &&
        (arg_s.X_md == 0) &&
        (arg_s.X_op == O_register) &&
        (arg_s.X_add_number == REG_A);

      if (is_generic)
        {
          if (!(ins_ok & INS_EZ80) && !sdcc_compat)
            ill_op();
          ++p;
          p = parse_exp(p, &arg_s);
        }
    }

  emit_sx(prefix, opcode, &arg_s);
  return p;
}

static const char *
emit_sub(char prefix, char opcode, const char *args)
{
  expressionS arg;
  const char *cursor;

  if (!(ins_ok & INS_GBZ80))
    return emit_s(prefix, opcode, args);

  if (args == NULL)
    {
      error(_("bad instruction syntax"));
      return args;
    }

  cursor = parse_exp(args, &arg);
  if (cursor == NULL)
    {
      error(_("bad instruction syntax"));
      return args;
    }

  if (*cursor != ',')
    {
      error(_("bad instruction syntax"));
      return cursor + 1;
    }
  cursor++;

  if (arg.X_md != 0 || arg.X_op != O_register || arg.X_add_number != REG_A)
    ill_op();

  cursor = parse_exp(cursor, &arg);

  emit_sx(prefix, opcode, &arg);
  return cursor;
}

static const char *
emit_swap(char prefix, char opcode, const char *args)
{
  if (!(ins_ok & INS_Z80N))
    return emit_mr(prefix, opcode, args);

  expressionS operand;
  const char *next = parse_exp(args, &operand);

  if (operand.X_md != 0 || operand.X_op != O_register || operand.X_add_number != REG_A)
    ill_op();

  enum { SWAPNIB_LEN = 2 };
  enum { SWAPNIB_PREFIX = 0xED, SWAPNIB_OPCODE = 0x23 };

  char *out = frag_more(SWAPNIB_LEN);
  if (out != NULL)
  {
    out[0] = (char)SWAPNIB_PREFIX;
    out[1] = (char)SWAPNIB_OPCODE;
  }

  return next;
}

static const char *
emit_call(char prefix ATTRIBUTE_UNUSED, char opcode, const char *args)
{
  expressionS addr;
  const char *p;
  char *q;

  p = parse_exp_not_indexed(args, &addr);
  if (addr.X_md)
  {
    ill_op();
    return p;
  }

  q = frag_more(1);
  if (q == NULL)
  {
    ill_op();
    return p;
  }

  *q = opcode;
  emit_word(&addr);
  return p;
}

/* Operand may be rr, r, (hl), (ix+d), (iy+d).  */
static const char *
emit_incdec(char prefix, char opcode, const char *args)
{
  expressionS operand;
  const char *p = parse_exp(args, &operand);

  if (!operand.X_md && operand.X_op == O_register)
    {
      int rnum = operand.X_add_number;
      if (R_ARITH & rnum)
        {
          char *q = frag_more((rnum & R_INDEX) ? 2 : 1);
          if (rnum & R_INDEX)
            *q++ = (rnum & R_IX) ? 0xDD : 0xFD;
          *q = (char)(prefix + ((rnum & 3) << 4));
          return p;
        }
    }

  if (operand.X_op == O_md1 || operand.X_op == O_register)
    emit_mx(0, opcode, 3, &operand);
  else
    ill_op();

  return p;
}

static const char *
emit_jr (char prefix ATTRIBUTE_UNUSED, char opcode, const char *args)
{
  expressionS addr;
  const char *rest = parse_exp_not_indexed (args, &addr);

  if (addr.X_md)
    {
      ill_op ();
      return rest;
    }

  {
    char *frag = frag_more (1);
    *frag = opcode;
    addr.X_add_number--;
    emit_byte (&addr, BFD_RELOC_8_PCREL);
  }

  return rest;
}

static const char *
emit_jp(char prefix, char opcode, const char *args)
{
  expressionS addr;
  const char *p = parse_exp_not_indexed(args, &addr);

  if (addr.X_md)
  {
    if (addr.X_op != O_register)
    {
      ill_op();
      return p;
    }

    int rnum = addr.X_add_number;
    int has_index = (rnum & R_INDEX) != 0;
    int base_reg = rnum & ~R_INDEX;

    if (base_reg == REG_HL)
    {
      char *q = frag_more(has_index ? 2 : 1);
      if (has_index)
        *q++ = (rnum & R_IX) ? 0xDD : 0xFD;
      *q = prefix;
      return p;
    }

    if (rnum == REG_C && (ins_ok & INS_Z80N))
    {
      char *q = frag_more(2);
      *q++ = 0xED;
      *q = 0x98;
      return p;
    }

    ill_op();
    return p;
  }

  {
    char *q = frag_more(1);
    *q = opcode;
    emit_word(&addr);
  }

  return p;
}

static const char *
emit_im(char prefix, char opcode, const char *args)
{
  expressionS mode;
  const char *p = parse_exp(args, &mode);

  if (mode.X_md || mode.X_op != O_constant) {
    ill_op();
    return p;
  }

  int add_num = mode.X_add_number;
  if (add_num == 1 || add_num == 2) {
    add_num++;
  } else if (add_num != 0) {
    ill_op();
    return p;
  }

  char *q = frag_more(2);
  if (q == NULL) {
    ill_op();
    return p;
  }

  *q++ = prefix;
  *q = (char)(opcode + 8 * add_num);

  return p;
}

static const char *
emit_pop(char prefix ATTRIBUTE_UNUSED, char opcode, const char *args)
{
  expressionS regp;
  const char *next = parse_exp(args, &regp);

  if (!regp.X_md && regp.X_op == O_register && (regp.X_add_number & R_STACKABLE))
    {
      const int rnum = regp.X_add_number;
      const int needs_index_prefix = (rnum & R_INDEX) != 0;
      char *out = frag_more(needs_index_prefix ? 2 : 1);

      if (needs_index_prefix)
        *out++ = (rnum & R_IX) ? (char)0xDD : (char)0xFD;

      *out = (char)(opcode + ((rnum & 3) << 4));
      return next;
    }

  ill_op();
  return next;
}

static const char *
emit_push(char prefix, char opcode, const char *args)
{
    enum { BYTES_OP = 2, BYTES_IMM = 2, OPCODE_PREFIX = 0xED, OPCODE_PUSHIMM = 0x8A };
    expressionS arg;
    const char *p;
    char *q;

    p = parse_exp(args, &arg);

    if (arg.X_op == O_register) {
        return emit_pop(prefix, opcode, args);
    }

    if (arg.X_md || arg.X_op == O_md1 || !(ins_ok & INS_Z80N)) {
        ill_op();
    }

    q = frag_more(BYTES_OP);
    if (q != NULL) {
        *q++ = (char)OPCODE_PREFIX;
        *q   = (char)OPCODE_PUSHIMM;
    }

    q = frag_more(BYTES_IMM);
    if (q != NULL) {
        fix_new_exp(frag_now, q - frag_now->fr_literal, BYTES_IMM, &arg, false, BFD_RELOC_Z80_16_BE);
    }

    return p;
}

static const char *
emit_retcc (char prefix ATTRIBUTE_UNUSED, char opcode, const char *args)
{
  char cc = 0;
  const char *parsed = parse_cc (args, &cc);
  char *out = frag_more (1);

  if (out != NULL)
    *out = parsed ? (char)(opcode + cc) : prefix;

  return parsed ? parsed : args;
}

static const char *
emit_adc(char prefix, char opcode, const char *args)
{
  expressionS term;
  const char *p = parse_exp(args, &term);

  if (*p != ',')
    {
      error(_("bad instruction syntax"));
      return p + 1;
    }
  p++;

  if (term.X_md || term.X_op != O_register)
    {
      ill_op();
      return p;
    }

  switch (term.X_add_number)
    {
    case REG_A:
      return emit_s(0, prefix, p);

    case REG_HL:
      p = parse_exp(p, &term);
      if (!term.X_md && term.X_op == O_register)
        {
          int rnum = term.X_add_number;
          if ((rnum & (R_ARITH | R_INDEX)) == R_ARITH)
            {
              char *q = frag_more(2);
              if (!q)
                {
                  error(_("out of memory"));
                  return p;
                }
              *q++ = (char) 0xED;
              *q = (char) (opcode + ((rnum & 3) << 4));
              return p;
            }
        }
      ill_op();
      return p;

    default:
      ill_op();
      return p;
    }
}

static const char *
emit_add(char prefix, char opcode, const char *args)
{
  expressionS term;
  int lhs, rhs;
  const char *p;
  char *q;

  p = parse_exp(args, &term);
  if (p == NULL || *p != ',')
    {
      error(_("bad instruction syntax"));
      return p ? p : args;
    }
  p++;

  if (term.X_md != 0 || term.X_op != O_register)
    {
      ill_op();
      return p;
    }

  switch (term.X_add_number)
    {
    case REG_A:
      p = emit_s(0, prefix, p);
      break;

    case REG_SP:
      p = parse_exp(p, &term);
      if (!(ins_ok & INS_GBZ80) || term.X_md || term.X_op == O_register)
        ill_op();
      q = frag_more(1);
      if (q != NULL)
        *q = 0xE8;
      emit_byte(&term, BFD_RELOC_Z80_DISP8);
      break;

    case REG_BC:
    case REG_DE:
      if (!(ins_ok & INS_Z80N))
        {
          ill_op();
          break;
        }
      /* Fall through.  */
    case REG_HL:
    case REG_IX:
    case REG_IY:
      lhs = term.X_add_number;
      p = parse_exp(p, &term);
      rhs = term.X_add_number;

      if (term.X_md != 0 || term.X_op == O_md1)
        {
          ill_op();
          break;
        }

      if ((term.X_op == O_register) && (rhs & R_ARITH)
          && (rhs == lhs || (rhs & ~R_INDEX) != REG_HL))
        {
          int need_index_prefix = (lhs & R_INDEX) ? 1 : 0;
          q = frag_more(need_index_prefix ? 2 : 1);
          if (q != NULL)
            {
              if (need_index_prefix)
                *q++ = (lhs & R_IX) ? 0xDD : 0xFD;
              *q = opcode + ((rhs & 3) << 4);
            }
          break;
        }

      if (!(lhs & R_INDEX) && (ins_ok & INS_Z80N))
        {
          if (term.X_op == O_register && rhs == REG_A)
            {
              q = frag_more(2);
              if (q != NULL)
                {
                  *q++ = 0xED;
                  *q = 0x33 - (lhs & 3);
                }
              break;
            }
          if (term.X_op != O_register && term.X_op != O_md1)
            {
              q = frag_more(2);
              if (q != NULL)
                {
                  *q++ = 0xED;
                  *q = 0x36 - (lhs & 3);
                }
              emit_word(&term);
              break;
            }
        }

      ill_op();
      break;

    default:
      ill_op();
      break;
    }

  return p;
}

static const char *
emit_bit(char prefix, char opcode, const char *args)
{
  expressionS b = {0};
  const char *p = parse_exp(args, &b);

  char sep = *p;
  p++;
  if (sep != ',')
    error(_("bad instruction syntax"));

  int bn = b.X_add_number;

  if (b.X_md || b.X_op != O_constant || bn < 0 || bn >= 8)
    {
      ill_op();
      return p;
    }

  int op = (int)(unsigned char) opcode;
  char code = (char)(op + (bn << 3));

  if (opcode == 0x40)
    p = emit_m(prefix, code, p);
  else
    p = emit_mr(prefix, code, p);

  return p;
}

/* BSLA DE,B; BSRA DE,B; BSRL DE,B; BSRF DE,B; BRLC DE,B (Z80N only) */
static const char *
emit_bshft(char prefix, char opcode, const char *args)
{
  expressionS r1, r2;
  const char *p = parse_exp(args, &r1);

  {
    char sep = *p;
    p++;
    if (sep != ',')
      error(_("bad instruction syntax"));
  }

  p = parse_exp(p, &r2);

  {
    int r1_ok = (!r1.X_md) && (r1.X_op == O_register) && (r1.X_add_number == REG_DE);
    int r2_ok = (!r2.X_md) && (r2.X_op == O_register) && (r2.X_add_number == REG_B);
    if (!(r1_ok && r2_ok))
      ill_op();
  }

  {
    char *q = frag_more(2);
    q[0] = prefix;
    q[1] = opcode;
  }

  return p;
}

static const char *
emit_jpcc(char prefix, char opcode, const char *args)
{
  char cc = 0;
  const char *p = parse_cc(args, &cc);

  if (p != NULL && *p == ',') {
    unsigned char op = (unsigned char)opcode + (unsigned char)cc;
    return emit_call(0, (char)op, p + 1);
  }

  if ((unsigned char)prefix == 0xC3) {
    return emit_jp(0xE9, prefix, args);
  }

  return emit_call(0, prefix, args);
}

static const char *
emit_jrcc(char prefix, char opcode, const char *args)
{
  if (args == NULL)
    return emit_jr(0, prefix, args);

  char cc_ch = 0;
  const char *p = parse_cc(args, &cc_ch);

  if (p != NULL && *p == ',')
    {
      p++;
      unsigned int cc = (unsigned int)(unsigned char)cc_ch;
      if (cc > (3u << 3))
        error(_("condition code invalid for jr"));
      else
        p = emit_jr(0, (char)(opcode + cc), p);
      return p;
    }

  return emit_jr(0, prefix, args);
}

static const char *
emit_ex (char prefix_in ATTRIBUTE_UNUSED,
         char opcode_in ATTRIBUTE_UNUSED, const char *args)
{
  expressionS op;
  const char *p;
  const char *q;
  char prefix, opcode;
  int key;
  int c1, c2;

  p = parse_exp_not_indexed (args, &op);
  p = skip_space (p);

  q = p;
  if (*q++ != ',')
    {
      error (_("bad instruction syntax"));
      return q;
    }
  p = q;

  prefix = 0;
  opcode = 0;

  if (op.X_op == O_register)
    {
      key = op.X_add_number | (op.X_md ? 0x8000 : 0);

      switch (key)
        {
        case REG_AF:
          q = p;
          c1 = 0;
          c2 = 0;
          if (*q)
            {
              c1 = TOLOWER (*q);
              q++;
            }
          if (*q)
            {
              c2 = TOLOWER (*q);
              q++;
            }
          if (c1 == 'a' && c2 == 'f')
            {
              if (*q == '`')
                q++;
              opcode = 0x08;
            }
          p = q;
          break;

        case REG_DE:
          q = p;
          c1 = 0;
          c2 = 0;
          if (*q)
            {
              c1 = TOLOWER (*q);
              q++;
            }
          if (*q)
            {
              c2 = TOLOWER (*q);
              q++;
            }
          if (c1 == 'h' && c2 == 'l')
            {
              opcode = 0xEB;
            }
          p = q;
          break;

        case (REG_SP | 0x8000):
          q = parse_exp (p, &op);
          if (op.X_op == O_register
              && op.X_md == 0
              && ((op.X_add_number & ~R_INDEX) == REG_HL))
            {
              opcode = 0xE3;
              if (R_INDEX & op.X_add_number)
                prefix = (R_IX & op.X_add_number) ? 0xDD : 0xFD;
            }
          p = q;
          break;

        default:
          break;
        }
    }

  if (opcode)
    emit_insn (prefix, opcode, p);
  else
    ill_op ();

  return p;
}

static const char *
emit_in(char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED, const char *args)
{
  expressionS reg, port;
  const char *p = parse_exp(args, &reg);
  char *q;

  if (reg.X_md && reg.X_op == O_register && reg.X_add_number == REG_C)
    {
      port = reg;
      reg.X_md = 0;
      reg.X_add_number = REG_F;
    }
  else
    {
      if (*p != ',')
        {
          error(_("bad instruction syntax"));
          return p;
        }
      p++;
      p = parse_exp(p, &port);
    }

  if (!(reg.X_md == 0
        && reg.X_op == O_register
        && (reg.X_add_number <= 7 || reg.X_add_number == REG_F)
        && port.X_md))
    {
      ill_op();
      return p;
    }

  if (port.X_op != O_md1 && port.X_op != O_register)
    {
      if (reg.X_add_number != REG_A)
        {
          ill_op();
          return p;
        }
      q = frag_more(1);
      *q = 0xDB;
      emit_byte(&port, BFD_RELOC_8);
      return p;
    }

  if (!(port.X_add_number == REG_C || port.X_add_number == REG_BC))
    {
      ill_op();
      return p;
    }

  if (port.X_add_number == REG_BC && !(ins_ok & INS_EZ80))
    {
      ill_op();
      return p;
    }

  if (reg.X_add_number == REG_F && !(ins_ok & (INS_R800 | INS_Z80N)))
    check_mach(INS_IN_F_C);

  q = frag_more(2);
  *q++ = 0xED;
  *q = 0x40 | ((reg.X_add_number & 7) << 3);

  return p;
}

static const char *
emit_in0 (char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED, const char *args)
{
  expressionS reg, port;
  const char *p = parse_exp (args, &reg);
  char sep = *p;

  p++;
  if (sep != ',')
    {
      error (_("bad instruction syntax"));
      return p;
    }

  p = parse_exp (p, &port);

  if (reg.X_md == 0
      && reg.X_op == O_register
      && reg.X_add_number >= 0
      && reg.X_add_number <= 7
      && port.X_md
      && port.X_op != O_md1
      && port.X_op != O_register)
    {
      unsigned int regnum = (unsigned int) reg.X_add_number;
      unsigned char *buf = (unsigned char *) frag_more (2);
      buf[0] = 0xED;
      buf[1] = (unsigned char) (regnum << 3);
      emit_byte (&port, BFD_RELOC_8);
      return p;
    }

  ill_op ();
  return p;
}

static const char *
emit_out (char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED, const char *args)
{
  expressionS reg, port;
  const char *p = parse_exp (args, &port);
  char *q;

  {
    char c = *p;
    p++;
    if (c != ',')
      {
        error (_("bad instruction syntax"));
        return p;
      }
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
      if (REG_A != reg.X_add_number)
        {
          ill_op ();
          return p;
        }

      q = frag_more (1);
      *q = 0xD3;
      emit_byte (&port, BFD_RELOC_8);
      return p;
    }

  if (!(port.X_add_number == REG_C || port.X_add_number == REG_BC))
    {
      ill_op ();
      return p;
    }

  if (port.X_add_number == REG_BC && !(ins_ok & INS_EZ80))
    {
      ill_op ();
      return p;
    }

  q = frag_more (2);
  *q++ = 0xED;
  *q = (char) (0x41 | (reg.X_add_number << 3));

  return p;
}

static const char *
emit_out0(char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED, const char *args)
{
  expressionS reg, port;
  const char *p = parse_exp(args, &port);

  if (p == NULL)
    {
      error(_("bad instruction syntax"));
      return args;
    }

  char sep = *p;
  p++;
  if (sep != ',')
    {
      error(_("bad instruction syntax"));
      return p;
    }

  p = parse_exp(p, &reg);

  int valid_port = (port.X_md != 0) && (port.X_op != O_register) && (port.X_op != O_md1);
  int valid_reg = (reg.X_md == 0) && (reg.X_op == O_register) && (reg.X_add_number <= 7);

  if (valid_port && valid_reg)
    {
      char *q = frag_more(2);
      if (q != NULL)
        {
          *q++ = 0xED;
          *q = (char)(0x01 | (reg.X_add_number << 3));
          emit_byte(&port, BFD_RELOC_8);
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

  return p;
}

static const char *
emit_rst (char prefix ATTRIBUTE_UNUSED, char opcode, const char *args)
{
  static const unsigned long RST_SHIFT = 3UL;
  static const unsigned long RST_MASK = 7UL << RST_SHIFT;

  expressionS expr;
  const char *p = parse_exp_not_indexed (args, &expr);
  if (expr.X_op != O_constant)
    {
      error ("rst needs constant address");
      return p;
    }

  {
    unsigned long val = (unsigned long) expr.X_add_number;
    if ((val & ~RST_MASK) != 0UL)
      {
        ill_op ();
        return p;
      }

    {
      char *out = frag_more (1);
      *out = (char) (opcode + (val & RST_MASK));
    }
  }

  return p;
}

/* For 8-bit indirect load to memory instructions like: LD (HL),n or LD (ii+d),n.  */
static void
emit_ld_m_n (expressionS *dst, expressionS *src)
{
  unsigned char prefix;
  unsigned int size;
  char *q;

  if (!dst || !src)
    {
      ill_op ();
      return;
    }

  switch (dst->X_add_number)
    {
    case REG_HL: prefix = 0x00; break;
    case REG_IX: prefix = 0xDD; break;
    case REG_IY: prefix = 0xFD; break;
    default:
      ill_op ();
      return;
    }

  size = prefix ? 2u : 1u;
  q = frag_more (size);
  if (!q)
    {
      ill_op ();
      return;
    }

  if (prefix)
    q[0] = (char) prefix;
  q[size - 1] = 0x36;

  if (prefix)
    {
      expressionS dst_offset = *dst;
      dst_offset.X_op = O_symbol;
      dst_offset.X_add_number = 0;
      emit_byte (&dst_offset, BFD_RELOC_Z80_DISP8);
    }

  emit_byte (src, BFD_RELOC_8);
}

/* For 8-bit load register to memory instructions: LD (<expression>),r.  */
static void
emit_ld_m_r (expressionS *dst, expressionS *src)
{
  if (dst == NULL || src == NULL)
    {
      ill_op ();
      return;
    }

  char prefix = 0;

  if (dst->X_op == O_md1)
    {
      if (ins_ok & INS_GBZ80)
        {
          if (src->X_op == O_register && src->X_add_number == REG_A)
            {
              *frag_more (1) = (dst->X_add_number == REG_HL) ? (char)0x22 : (char)0x32;
              return;
            }
          ill_op ();
          return;
        }
      prefix = (dst->X_add_number == REG_IX) ? (char)0xDD : (char)0xFD;
      /* Fall through to register handling.  */
    }

  if (dst->X_op == O_register || prefix)
    {
      switch (dst->X_add_number)
        {
        case REG_BC:
        case REG_DE:
          if (src->X_add_number == REG_A)
            {
              char *q = frag_more (1);
              *q = (char)(0x02 | ((dst->X_add_number & 3) << 4));
              return;
            }
          ill_op ();
          return;

        case REG_IX:
        case REG_IY:
        case REG_HL:
          if (src->X_add_number <= 7)
            {
              char *q = frag_more (prefix ? 2 : 1);
              if (prefix)
                *q++ = prefix;
              *q = (char)(0x70 | (src->X_add_number & 0x7));
              if (prefix)
                {
                  expressionS dst_offset = *dst;
                  dst_offset.X_op = O_symbol;
                  dst_offset.X_add_number = 0;
                  emit_byte (&dst_offset, BFD_RELOC_Z80_DISP8);
                }
              return;
            }
          ill_op ();
          return;

        default:
          ill_op ();
          return;
        }
    }

  if (src->X_add_number == REG_A)
    {
      char *q = frag_more (1);
      *q = (char)((ins_ok & INS_GBZ80) ? 0xEA : 0x32);
      emit_word (dst);
      return;
    }

  ill_op ();
}

/* For 16-bit load register to memory instructions: LD (<expression>),rr.  */
static void
emit_ld_m_rr (expressionS *dst, expressionS *src)
{
  int prefix = 0;
  int opcode = 0;

  switch (dst->X_op)
    {
    case O_md1:
    case O_register:
      {
        if (!(ins_ok & INS_EZ80))
          {
            ill_op ();
            return;
          }

        switch (dst->X_add_number)
          {
          case REG_IX: prefix = 0xDD; break;
          case REG_IY: prefix = 0xFD; break;
          case REG_HL: prefix = 0xED; break;
          default:
            ill_op ();
            return;
          }

        switch (src->X_add_number)
          {
          case REG_BC: opcode = 0x0F; break;
          case REG_DE: opcode = 0x1F; break;
          case REG_HL: opcode = 0x2F; break;
          case REG_IX: opcode = (prefix == 0xFD) ? 0x3E : 0x3F; break;
          case REG_IY: opcode = (prefix == 0xFD) ? 0x3F : 0x3E; break;
          default:
            ill_op ();
            return;
          }

        {
          char *q = frag_more (2);
          q[0] = (char) prefix;
          q[1] = (char) opcode;
        }

        if (prefix == 0xFD || prefix == 0xDD)
          {
            expressionS dst_offset = *dst;
            dst_offset.X_op = O_symbol;
            dst_offset.X_add_number = 0;
            emit_byte (&dst_offset, BFD_RELOC_Z80_DISP8);
          }
        break;
      }

    default:
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
              case REG_BC: prefix = 0xED; opcode = 0x43; break;
              case REG_DE: prefix = 0xED; opcode = 0x53; break;
              case REG_HL: prefix = 0x00; opcode = 0x22; break;
              case REG_IX: prefix = 0xDD; opcode = 0x22; break;
              case REG_IY: prefix = 0xFD; opcode = 0x22; break;
              case REG_SP: prefix = 0xED; opcode = 0x73; break;
              default:
                ill_op ();
                return;
              }
          }

        {
          char *q = frag_more (prefix ? 2 : 1);
          if (prefix)
            {
              q[0] = (char) prefix;
              q[1] = (char) opcode;
            }
          else
            {
              q[0] = (char) opcode;
            }
        }

        emit_word (dst);
        break;
      }
    }
}

static void
emit_ld_r_m(expressionS *dst, expressionS *src)
{
  char *q;
  char prefix = 0;
  char opcode = 0;
  expressionS src_offset;

  if (dst == NULL || src == NULL)
    {
      ill_op();
      return;
    }

  if (dst->X_add_number == REG_A && src->X_op == O_register)
    {
      switch (src->X_add_number)
        {
        case REG_BC: opcode = 0x0A; break;
        case REG_DE: opcode = 0x1A; break;
        default: break;
        }
      if (opcode != 0)
        {
          q = frag_more(1);
          *q = opcode;
          return;
        }
    }

  switch (src->X_op)
    {
    case O_md1:
      if (ins_ok & INS_GBZ80)
        {
          if (dst->X_op == O_register && dst->X_add_number == REG_A)
            *frag_more(1) = (src->X_add_number == REG_HL) ? 0x2A : 0x3A;
          else
            ill_op();
          break;
        }
    case O_register:
      opcode = 0x46;
      if (dst->X_add_number > 7)
        ill_op();
      switch (src->X_add_number)
        {
        case REG_HL: prefix = 0x00; break;
        case REG_IX: prefix = 0xDD; break;
        case REG_IY: prefix = 0xFD; break;
        default:
          ill_op();
          prefix = 0x00;
          break;
        }
      q = frag_more(prefix ? 2 : 1);
      if (prefix)
        *q++ = prefix;
      *q = opcode | ((dst->X_add_number & 7) << 3);
      if (prefix)
        {
          src_offset = *src;
          src_offset.X_op = O_symbol;
          src_offset.X_add_number = 0;
          emit_byte(&src_offset, BFD_RELOC_Z80_DISP8);
        }
      break;
    default:
      if (dst->X_add_number == REG_A)
        {
          q = frag_more(1);
          *q = (ins_ok & INS_GBZ80) ? 0xFA : 0x3A;
          emit_word(src);
        }
      else
        ill_op();
    }
}

static void
emit_ld_r_n(expressionS *dst, expressionS *src)
{
  enum { PREFIX_IX = 0xDD, PREFIX_IY = 0xFD, LD_R_N_BASE = 0x06 };

  if (dst == NULL || src == NULL)
    {
      ill_op();
      return;
    }

  unsigned int reg = (unsigned int) dst->X_add_number;
  unsigned char prefix = 0;

  switch (reg)
    {
    case REG_H | R_IX:
    case REG_L | R_IX:
      prefix = PREFIX_IX;
      break;
    case REG_H | R_IY:
    case REG_L | R_IY:
      prefix = PREFIX_IY;
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
      ill_op();
      break;
    }

  char *buf = frag_more(prefix ? 2 : 1);

  if (prefix)
    {
      if ((ins_ok & INS_GBZ80) != 0)
        ill_op();
      else if ((ins_ok & (INS_EZ80 | INS_R800 | INS_Z80N)) == 0)
        check_mach(INS_IDX_HALF);
      *buf++ = (char) prefix;
    }

  unsigned int regbits = reg & 7U;
  unsigned char opcode = (unsigned char) (LD_R_N_BASE | (unsigned char) (regbits << 3));
  *buf = (char) opcode;

  emit_byte(src, BFD_RELOC_8);
}

static void
emit_ld_r_r (expressionS *dst, expressionS *src)
{
  int prefix = 0;
  int opcode = 0;
  int ii_halves = 0;
  int ds = dst->X_add_number;
  int ss = src->X_add_number;

  switch (ds)
    {
    case REG_SP:
      switch (ss)
        {
        case REG_HL: prefix = 0x00; break;
        case REG_IX: prefix = 0xDD; break;
        case REG_IY: prefix = 0xFD; break;
        default:
          ill_op ();
        }
      opcode = 0xF9;
      break;

    case REG_HL:
      if (!(ins_ok & INS_EZ80))
        ill_op ();
      if (ss != REG_I)
        ill_op ();
      if (cpu_mode < 1)
        error (_("ADL mode instruction"));
      prefix = 0xED;
      opcode = 0xD7;
      break;

    case REG_I:
      if (ss == REG_HL)
        {
          if (!(ins_ok & INS_EZ80))
            ill_op ();
          if (cpu_mode < 1)
            error (_("ADL mode instruction"));
          prefix = 0xED;
          opcode = 0xC7;
        }
      else if (ss == REG_A)
        {
          prefix = 0xED;
          opcode = 0x47;
        }
      else
        ill_op ();
      break;

    case REG_MB:
      if (!(ins_ok & INS_EZ80) || (ss != REG_A))
        ill_op ();
      if (cpu_mode < 1)
        error (_("ADL mode instruction"));
      prefix = 0xED;
      opcode = 0x6D;
      break;

    case REG_R:
      if (ss == REG_A)
        {
          prefix = 0xED;
          opcode = 0x4F;
        }
      else
        ill_op ();
      break;

    case REG_A:
      if (ss == REG_I)
        {
          prefix = 0xED;
          opcode = 0x57;
          break;
        }
      else if (ss == REG_R)
        {
          prefix = 0xED;
          opcode = 0x5F;
          break;
        }
      else if (ss == REG_MB)
        {
          if (!(ins_ok & INS_EZ80))
            ill_op ();
          else
            {
              if (cpu_mode < 1)
                error (_("ADL mode instruction"));
              prefix = 0xED;
              opcode = 0x6E;
            }
          break;
        }
      /* fall through */

    case REG_B:
    case REG_C:
    case REG_D:
    case REG_E:
    case REG_H:
    case REG_L:
      prefix = 0x00;
      break;

    case (REG_H | R_IX):
    case (REG_L | R_IX):
      prefix = 0xDD;
      ii_halves = 1;
      break;

    case (REG_H | R_IY):
    case (REG_L | R_IY):
      prefix = 0xFD;
      ii_halves = 1;
      break;

    default:
      ill_op ();
    }

  if (opcode == 0)
    {
      switch (ss)
        {
        case REG_A:
        case REG_B:
        case REG_C:
        case REG_D:
        case REG_E:
          break;

        case REG_H:
        case REG_L:
          if (prefix != 0)
            ill_op ();
          break;

        case (REG_H | R_IX):
        case (REG_L | R_IX):
          if (prefix == 0xFD || ds == REG_H || ds == REG_L)
            ill_op ();
          prefix = 0xDD;
          ii_halves = 1;
          break;

        case (REG_H | R_IY):
        case (REG_L | R_IY):
          if (prefix == 0xDD || ds == REG_H || ds == REG_L)
            ill_op ();
          prefix = 0xFD;
          ii_halves = 1;
          break;

        default:
          ill_op ();
        }
      opcode = 0x40 + (((ds & 7) << 3) + (ss & 7));
    }

  if ((ins_ok & INS_GBZ80) && prefix != 0)
    ill_op ();

  if (ii_halves && !(ins_ok & (INS_EZ80 | INS_R800 | INS_Z80N)))
    check_mach (INS_IDX_HALF);

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
        default:
          break;
        }
    }

  {
    unsigned char *out = (unsigned char *) frag_more (prefix ? 2 : 1);
    if (prefix)
      {
        out[0] = (unsigned char) prefix;
        out[1] = (unsigned char) opcode;
      }
    else
      {
        out[0] = (unsigned char) opcode;
      }
  }
}

static void
emit_ld_rr_m(expressionS *dst, expressionS *src)
{
  char *q;
  int prefix = 0xED;
  int opcode = 0;
  expressionS src_offset;

  if (ins_ok & INS_GBZ80)
    ill_op();

  if (dst == NULL || src == NULL) {
    ill_op();
    return;
  }

  switch (src->X_op)
  {
    case O_md1:
    case O_register:
    {
      int has_disp = (src->X_op == O_md1);

      if (!(ins_ok & INS_EZ80))
        ill_op();

      if (has_disp)
        prefix = (src->X_add_number == REG_IX) ? 0xDD : 0xFD;

      switch (dst->X_add_number)
      {
        case REG_BC: opcode = 0x07; break;
        case REG_DE: opcode = 0x17; break;
        case REG_HL: opcode = 0x27; break;
        case REG_IX: opcode = ((prefix == 0xED) || (prefix == 0xDD)) ? 0x37 : 0x31; break;
        case REG_IY: opcode = ((prefix == 0xED) || (prefix == 0xDD)) ? 0x31 : 0x37; break;
        default:
          ill_op();
      }

      q = frag_more(2);
      q[0] = (char) prefix;
      q[1] = (char) opcode;

      if (has_disp)
      {
        src_offset = *src;
        src_offset.X_op = O_symbol;
        src_offset.X_add_number = 0;
        emit_byte(&src_offset, BFD_RELOC_Z80_DISP8);
      }
      break;
    }

    default:
    {
      switch (dst->X_add_number)
      {
        case REG_BC: prefix = 0xED; opcode = 0x4B; break;
        case REG_DE: prefix = 0xED; opcode = 0x5B; break;
        case REG_HL: prefix = 0x00; opcode = 0x2A; break;
        case REG_SP: prefix = 0xED; opcode = 0x7B; break;
        case REG_IX: prefix = 0xDD; opcode = 0x2A; break;
        case REG_IY: prefix = 0xFD; opcode = 0x2A; break;
        default:
          ill_op();
      }

      q = frag_more(prefix ? 2 : 1);
      if (prefix)
      {
        q[0] = (char) prefix;
        q[1] = (char) opcode;
      }
      else
      {
        q[0] = (char) opcode;
      }

      emit_word(src);
      break;
    }
  }
}

static void
emit_ld_rr_nn(expressionS *dst, expressionS *src)
{
  char *buf;
  int prefix = 0x00;
  int opcode = 0x21;
  int reg;

  if (dst == NULL || src == NULL)
    {
      ill_op();
      return;
    }

  reg = dst->X_add_number;

  switch (reg)
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
      opcode = 0x01 + ((reg & 3) << 4);
      break;
    default:
      ill_op();
      return;
    }

  if (prefix && (ins_ok & INS_GBZ80))
    ill_op();

  buf = frag_more(prefix ? 2 : 1);
  if (buf == NULL)
    {
      ill_op();
      return;
    }

  if (prefix)
    *buf++ = (char) prefix;

  *buf = (char) opcode;
  emit_word(src);
}

static int is_8bit_reg_num(long long n) { return n <= 7; }
static int is_8bit_reg_indexed_num(long long n) { return ((n & ~((long long)R_INDEX)) <= 7); }

static const char *
emit_ld (char prefix_in ATTRIBUTE_UNUSED, char opcode_in ATTRIBUTE_UNUSED, const char *args)
{
  expressionS dst, src;
  const char *p = parse_exp(args, &dst);

  char sep = *p;
  p++;
  if (sep != ',')
    error(_("bad instruction syntax"));

  p = parse_exp(p, &src);

  if (dst.X_md)
    {
      if (src.X_op == O_register)
        {
          if (is_8bit_reg_num(src.X_add_number))
            emit_ld_m_r(&dst, &src);
          else
            emit_ld_m_rr(&dst, &src);
        }
      else
        {
          emit_ld_m_n(&dst, &src);
        }
    }
  else if (dst.X_op == O_register)
    {
      if (src.X_md)
        {
          if (is_8bit_reg_num(dst.X_add_number))
            emit_ld_r_m(&dst, &src);
          else
            emit_ld_rr_m(&dst, &src);
        }
      else if (src.X_op == O_register)
        {
          emit_ld_r_r(&dst, &src);
        }
      else if (is_8bit_reg_indexed_num(dst.X_add_number))
        {
          emit_ld_r_n(&dst, &src);
        }
      else
        {
          emit_ld_rr_nn(&dst, &src);
        }
    }
  else
    {
      ill_op();
    }

  return p;
}

static const char *
emit_lddldi (char prefix, char opcode, const char *args)
{
  expressionS dst, src;
  const char *p_after_dst, *p_after_comma, *p_end;
  unsigned char code;
  char *out;

  if (!(ins_ok & INS_GBZ80))
    return emit_insn(prefix, opcode, args);

  if (args == NULL)
  {
    error(_("bad instruction syntax"));
    return args;
  }

  p_after_dst = parse_exp(args, &dst);
  p_after_comma = p_after_dst;

  if (*p_after_comma != ',')
    error(_("bad instruction syntax"));
  p_after_comma++;

  p_end = parse_exp(p_after_comma, &src);

  if (dst.X_op != O_register || src.X_op != O_register)
    ill_op();

  code = (unsigned char)(((opcode & 0x08) << 1) + 0x22);

  if (dst.X_md != 0
      && dst.X_add_number == REG_HL
      && src.X_md == 0
      && src.X_add_number == REG_A)
    code |= 0x00;
  else if (dst.X_md == 0
           && dst.X_add_number == REG_A
           && src.X_md != 0
           && src.X_add_number == REG_HL)
    code |= 0x08;
  else
    ill_op();

  out = frag_more(1);
  *out = (char)code;
  return p_end;
}

static const char *
emit_ldh (char prefix ATTRIBUTE_UNUSED, char opcode ATTRIBUTE_UNUSED, const char *args)
{
  expressionS dst, src;
  const char *p = parse_exp (args, &dst);

  if (*p != ',')
    {
      p++;
      error (_("bad instruction syntax"));
      return p;
    }
  p++;

  p = parse_exp (p, &src);

  {
    const int dst_is_a = (dst.X_md == 0 && dst.X_op == O_register && dst.X_add_number == REG_A);
    const int src_is_mem = (src.X_md != 0 && src.X_op != O_md1);

    if (dst_is_a && src_is_mem)
      {
        if (src.X_op != O_register)
          {
            *frag_more (1) = 0xF0;
            emit_byte (&src, BFD_RELOC_8);
          }
        else if (src.X_add_number == REG_C)
          {
            *frag_more (1) = 0xF2;
          }
        else
          {
            ill_op ();
          }
        return p;
      }
  }

  {
    const int dst_is_mem = (dst.X_md != 0 && dst.X_op != O_md1);
    const int src_is_a_reg = (src.X_md == 0 && src.X_op == O_register && src.X_add_number == REG_A);

    if (dst_is_mem && src_is_a_reg)
      {
        if (dst.X_op == O_register)
          {
            if (dst.X_add_number == REG_C)
              {
                *frag_more (1) = 0xE2;
              }
            else
              {
                ill_op ();
              }
          }
        else
          {
            *frag_more (1) = 0xE0;
            emit_byte (&dst, BFD_RELOC_8);
          }
      }
    else
      {
        ill_op ();
      }
  }

  return p;
}

static const char *
emit_ldhl (char prefix ATTRIBUTE_UNUSED, char opcode, const char *args)
{
  expressionS dst, src;
  const char *p;
  char comma;
  char *q;

  (void) prefix;

  p = parse_exp (args, &dst);

  comma = *p;
  p++;
  if (comma != ',')
    {
      error (_("bad instruction syntax"));
      return p;
    }

  p = parse_exp (p, &src);

  {
    int invalid_dst = (dst.X_md != 0) || (dst.X_op != O_register) || (dst.X_add_number != REG_SP);
    int invalid_src = (src.X_md != 0) || (src.X_op == O_register) || (src.X_op == O_md1);
    if (invalid_dst || invalid_src)
      ill_op ();
  }

  q = frag_more (1);
  *q = opcode;
  emit_byte (&src, BFD_RELOC_Z80_DISP8);
  return p;
}

static const char *
parse_lea_pea_args (const char * args, expressionS *op)
{
  if (args == NULL || op == NULL)
    return args;

  const char *p = parse_exp (args, op);
  if (p == NULL)
    return NULL;

  if (sdcc_compat && *p == ',' && op->X_op == O_register)
    {
      expressionS off;
      const char *next = parse_exp (p + 1, &off);
      if (next == NULL)
        return NULL;
      op->X_op = O_add;
      op->X_add_symbol = make_expr_symbol (&off);
      p = next;
    }

  return p;
}

static const char *
emit_lea (char prefix, char opcode, const char * args)
{
  expressionS dst, src;
  const char *p;
  int dst_rnum, src_rnum;

  p = parse_exp (args, &dst);
  if (dst.X_md != 0 || dst.X_op != O_register)
    ill_op ();

  dst_rnum = dst.X_add_number;
  switch (dst_rnum)
    {
    case REG_BC:
    case REG_DE:
    case REG_HL:
      opcode = (char) (0x02 | ((dst_rnum & 0x03) << 4));
      break;
    case REG_IX:
      opcode = (char) 0x32;
      break;
    case REG_IY:
      opcode = (char) 0x33;
      break;
    default:
      ill_op ();
      break;
    }

  if (p == NULL || *p++ != ',')
    error (_("bad instruction syntax"));

  p = parse_lea_pea_args (p, &src);
  if (src.X_md != 0)
    ill_op ();

  switch (src.X_op)
    {
    case O_add:
      src_rnum = src.X_add_number;
      break;
    case O_register:
      src.X_add_symbol = zero;
      src_rnum = src.X_add_number;
      break;
    default:
      ill_op ();
      src_rnum = 0;
      break;
    }

  switch (src_rnum)
    {
    case REG_IX:
      opcode = (char) ((opcode == (char) 0x33) ? 0x55 : (opcode | 0x00));
      break;
    case REG_IY:
      opcode = (char) ((opcode == (char) 0x32) ? 0x54 : (opcode | 0x01));
      break;
    default:
      break;
    }

  {
    unsigned char *q = (unsigned char *) frag_more (2);
    q[0] = (unsigned char) prefix;
    q[1] = (unsigned char) opcode;
  }

  src.X_op = O_symbol;
  src.X_add_number = 0;
  emit_byte (&src, BFD_RELOC_Z80_DISP8);

  return p;
}

static const char *
emit_mlt(char prefix, char opcode, const char *args)
{
  expressionS arg;
  const char *p = parse_exp(args, &arg);

  if (arg.X_md != 0 || arg.X_op != O_register || ((arg.X_add_number & R_ARITH) == 0))
    ill_op();

  char *q = frag_more(2);
  if ((ins_ok & INS_Z80N) != 0)
    {
      if (arg.X_add_number != REG_DE)
        ill_op();
      q[0] = (char)0xED;
      q[1] = (char)0x30;
    }
  else
    {
      q[0] = prefix;
      q[1] = (char)(opcode | ((arg.X_add_number & 3) << 4));
    }

  return p;
}

/* MUL D,E (Z80N only) */
static const char *
emit_mul(char prefix, char opcode, const char *args)
{
  expressionS r1, r2;
  const char *p;

  p = parse_exp(args, &r1);

  {
    char sep = *p;
    p++;
    if (sep != ',')
      error(_("bad instruction syntax"));
  }

  p = parse_exp(p, &r2);

  {
    int r1_ok = (r1.X_md == 0 && r1.X_op == O_register && r1.X_add_number == REG_D);
    int r2_ok = (r2.X_md == 0 && r2.X_op == O_register && r2.X_add_number == REG_E);
    if (!r1_ok || !r2_ok)
      ill_op();
  }

  {
    char *buf = frag_more(2);
    buf[0] = prefix;
    buf[1] = opcode;
  }

  return p;
}

static const char *
emit_nextreg (char prefix, char opcode ATTRIBUTE_UNUSED, const char * args)
{
  expressionS rr, nn;
  const char *p = parse_exp (args, &rr);
  char sep = *p;
  p++;
  if (sep != ',')
    error (_("bad instruction syntax"));

  p = parse_exp (p, &nn);

  if ((rr.X_md != 0) || (rr.X_op == O_register) || (rr.X_op == O_md1)
      || (nn.X_md != 0) || (nn.X_op == O_md1))
    ill_op ();

  char *q = frag_more (2);
  *q++ = prefix;
  emit_byte (&rr, BFD_RELOC_8);

  if (nn.X_op == O_register)
    {
      if (nn.X_add_number == REG_A)
        *q = 0x92;
      else
        ill_op ();
    }
  else
    {
      *q = 0x91;
      emit_byte (&nn, BFD_RELOC_8);
    }

  return p;
}

static const char *
emit_pea(char prefix, char opcode, const char *args)
{
  expressionS arg;
  const char *p;

  if (args == NULL)
    {
      ill_op();
      return NULL;
    }

  p = parse_lea_pea_args(args, &arg);
  if (p == NULL)
    {
      ill_op();
      return NULL;
    }

  if (arg.X_md != 0 || arg.X_op != O_add || (arg.X_add_number & R_INDEX) == 0)
    {
      ill_op();
    }

  {
    char *buf = frag_more(2);
    const int is_iy = (arg.X_add_number == REG_IY) ? 1 : 0;
    buf[0] = (char)(unsigned char) prefix;
    buf[1] = (char)(unsigned char) (opcode + is_iy);
  }

  arg.X_op = O_symbol;
  arg.X_add_number = 0;
  emit_byte(&arg, BFD_RELOC_Z80_DISP8);

  return p;
}

static const char *
emit_reti (char prefix, char opcode, const char * args)
{
  const int is_gbz80 = (ins_ok & INS_GBZ80) != 0;
  const char effective_prefix = is_gbz80 ? (char)0x00 : prefix;
  const char effective_opcode = is_gbz80 ? (char)0xD9 : opcode;
  return emit_insn(effective_prefix, effective_opcode, args);
}

static const char *
emit_tst(char prefix, char opcode, const char *args)
{
  expressionS arg_s;
  const char *p = parse_exp(args, &arg_s);

  if (p != NULL && *p == ',' && arg_s.X_md == 0 && arg_s.X_op == O_register && arg_s.X_add_number == REG_A)
    {
      if (!(ins_ok & INS_EZ80))
        ill_op();
      p++;
      p = parse_exp(p, &arg_s);
    }

  {
    int rnum = arg_s.X_add_number;

    switch (arg_s.X_op)
      {
      case O_md1:
        ill_op();
        break;

      case O_register:
        {
          if (arg_s.X_md != 0)
            {
              if (rnum != REG_HL)
                ill_op();
              else
                rnum = 6;
            }
          unsigned char *q = (unsigned char *) frag_more(2);
          q[0] = (unsigned char) prefix;
          q[1] = (unsigned char) (opcode | (rnum << 3));
          break;
        }

      default:
        {
          if (arg_s.X_md)
            ill_op();
          unsigned char *q = (unsigned char *) frag_more(2);
          if (ins_ok & INS_Z80N)
            {
              q[0] = 0xED;
              q[1] = 0x27;
            }
          else
            {
              q[0] = (unsigned char) prefix;
              q[1] = (unsigned char) (opcode | 0x60);
            }
          emit_byte(&arg_s, BFD_RELOC_8);
          break;
        }
      }
  }

  return p;
}

static const char *
emit_insn_n (char prefix, char opcode, const char *args)
{
  expressionS expr = {0};
  const char *rest;
  char *buf;

  rest = parse_exp (args, &expr);
  if (expr.X_md || expr.X_op == O_register || expr.X_op == O_md1)
    ill_op ();

  buf = frag_more (2);
  if (buf == NULL)
    ill_op ();

  buf[0] = prefix;
  buf[1] = opcode;
  emit_byte (&expr, BFD_RELOC_8);

  return rest;
}

static void
emit_data (int size ATTRIBUTE_UNUSED)
{
  const char *p;

  (void) size;

  if (is_it_end_of_statement())
    {
      demand_empty_rest_of_line();
      return;
    }

  p = skip_space(input_line_pointer);

  {
    int broke_early = 0;

    for (;;)
      {
        if (*p == '\"' || *p == '\'')
          {
            char quote = *p++;
            const char *q = p;
            int cnt = 0;

            while (*p && *p != quote)
              {
                ++p;
                ++cnt;
              }

            {
              char *u = frag_more(cnt);
              (void) u;
              if (cnt > 0)
                memcpy(u, q, (size_t) cnt);
            }

            if (!*p)
              {
                as_warn(_("unterminated string"));
              }
            else
              {
                ++p;
                p = skip_space(p);
              }
          }
        else
          {
            expressionS exp;
            const char *np = parse_exp(p, &exp);

            if (exp.X_op == O_md1 || exp.X_op == O_register)
              {
                ill_op();
                broke_early = 1;
                break;
              }

            if (exp.X_md)
              as_warn(_("parentheses ignored"));

            emit_byte(&exp, BFD_RELOC_8);
            p = skip_space(np);
          }

        if (*p != ',')
          break;

        ++p;
      }

    input_line_pointer = (char *) (broke_early ? (p - 1) : p);
  }
}

static void z80_cons(int size)
{
  const char *p;
  const char *end_pos;
  expressionS exp;

  if (is_it_end_of_statement())
    {
      demand_empty_rest_of_line();
      return;
    }

  p = skip_space(input_line_pointer);
  end_pos = p;

  for (;;)
    {
      const char *next = parse_exp(p, &exp);

      if (exp.X_op == O_md1 || exp.X_op == O_register)
        {
          ill_op();
          end_pos = next ? next - 1 : p;
          break;
        }

      if (exp.X_md)
        as_warn(_("parentheses ignored"));

      emit_data_val(&exp, size);

      p = skip_space(next);

      if (*p != ',')
        {
          end_pos = p;
          break;
        }

      p++;
    }

  input_line_pointer = (char *) end_pos;
}

/* next functions were commented out because it is difficult to mix
   both ADL and Z80 mode instructions within one COFF file:
   objdump cannot recognize point of mode switching.
*/
static void set_cpu_mode(int mode)
{
    if ((ins_ok & INS_EZ80) == 0) {
        error(_("CPU mode is unsupported by target"));
        return;
    }

    if (cpu_mode == mode) {
        return;
    }

    cpu_mode = mode;
}

static void
assume (int arg ATTRIBUTE_UNUSED)
{
  char *name = NULL;
  char c;
  int n;

  input_line_pointer = (char *) skip_space (input_line_pointer);
  c = get_symbol_name (&name);
  if (name == NULL || strncasecmp (name, "ADL", 4) != 0)
    {
      ill_op ();
      return;
    }

  restore_line_pointer (c);
  input_line_pointer = (char *) skip_space (input_line_pointer);

  {
    char eq = *input_line_pointer;
    if (eq != '=')
      {
        error (_("assignment expected"));
        input_line_pointer++;
        return;
      }
    input_line_pointer++;
  }

  input_line_pointer = (char *) skip_space (input_line_pointer);
  n = get_single_number ();

  set_cpu_mode (n);
}

static const char *
emit_mulub (char prefix ATTRIBUTE_UNUSED, char opcode, const char * args)
{
  const char *p = skip_space (args);
  const char first = TOLOWER (*p);
  const char second = (first != '\0') ? *(p + 1) : '\0';

  if (first != 'a' || second != ',')
    {
      if (*p != '\0')
        ++p;
      if (*p != '\0')
        ++p;
      ill_op ();
      return p;
    }

  p += 2;

  char reg = TOLOWER (*p++);
  if (reg == 'b' || reg == 'c' || reg == 'd' || reg == 'e')
    {
      check_mach (INS_R800);
      if (!*skip_space (p))
        {
          char *q = frag_more (2);
          *q++ = prefix;
          *q = opcode + ((reg - 'b') << 3);
          return p;
        }
    }

  ill_op ();
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

  {
    expressionS reg;
    p = parse_exp (p, &reg);

    if ((!reg.X_md) && reg.X_op == O_register)
      {
        switch (reg.X_add_number)
          {
          case REG_BC:
          case REG_SP:
            {
              check_mach (INS_R800);
              char *buf = frag_more (2);
              buf[0] = prefix;
              buf[1] = opcode + ((reg.X_add_number & 3) << 4);
              break;
            }
          default:
            ill_op ();
          }
      }
  }

  return p;
}

static int suffix_cmp(const void *key, const void *elem)
{
  const char *s = (const char *) key;
  const char *e = ((const char (*)[4])elem)[0];
  return strcmp(s, e);
}

static const struct {
  char str[4];
  unsigned char code_cpu0;
  unsigned char code_cpu1;
} suffix_map[] = {
  { "il",  0x52, 0x5B },
  { "is",  0x40, 0x49 },
  { "l",   0x49, 0x5B },
  { "lil", 0x5B, 0x5B },
  { "lis", 0x49, 0x49 },
  { "s",   0x40, 0x52 },
  { "sil", 0x52, 0x52 },
  { "sis", 0x40, 0x40 }
};

static int
assemble_suffix (const char **suffix)
{
  const char *p;
  char sbuf[4];
  int i;

  if (suffix == NULL || *suffix == NULL)
    return 0;

  p = *suffix;
  if (*p++ != '.')
    return 0;

  for (i = 0; (i < 3) && ISALPHA (*p); i++)
    sbuf[i] = TOLOWER (*p++);
  if (*p && !is_whitespace (*p))
    return 0;
  sbuf[i] = 0;
  *suffix = p;

  const void *t = bsearch (sbuf, suffix_map, ARRAY_SIZE (suffix_map),
                           sizeof (suffix_map[0]), suffix_cmp);
  if (t == NULL)
    return 0;

  const unsigned char code = cpu_mode
                             ? ((const typeof(suffix_map[0]) *)t)->code_cpu1
                             : ((const typeof(suffix_map[0]) *)t)->code_cpu0;

  *frag_more (1) = (char) code;

  switch (code)
    {
    case 0x40: inst_mode = INST_MODE_FORCED | INST_MODE_S | INST_MODE_IS; break;
    case 0x49: inst_mode = INST_MODE_FORCED | INST_MODE_L | INST_MODE_IS; break;
    case 0x52: inst_mode = INST_MODE_FORCED | INST_MODE_S | INST_MODE_IL; break;
    case 0x5B: inst_mode = INST_MODE_FORCED | INST_MODE_L | INST_MODE_IL; break;
    default: return 0;
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

static void set_inss(int inss)
{
    if (!sdcc_compat) {
        as_fatal(_("Invalid directive"));
    }

    const int previous_ins = ins_ok;
    const int new_ins = (ins_ok & INS_MARCH_MASK) | inss;

    ins_ok = new_ins;

    if (previous_ins != new_ins) {
        cpu_mode = 0;
    }
}

static void ignore(int arg)
{
    (void)arg;
    ignore_rest_of_line();
}

static void
area(int arg)
{
  char *cursor;
  int has_open_paren = 0;

  if (!sdcc_compat) {
    as_fatal(_("Invalid directive"));
    return;
  }

  cursor = input_line_pointer;
  if (cursor != NULL) {
    while (*cursor != '\0' && *cursor != '(' && *cursor != '\n') {
      cursor++;
    }
    if (*cursor == '(') {
      has_open_paren = 1;
      *cursor = '\n';
    }
  }

  psect(arg);

  if (has_open_paren && cursor != NULL) {
    *cursor = '(';
    ignore_rest_of_line();
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
md_assemble(char *str)
{
  const char *p;
  char *old_ptr;
  size_t i = 0;
  table_t *insp;

  err_flag = 0;
  inst_mode = cpu_mode ? (INST_MODE_L | INST_MODE_IL) : (INST_MODE_S | INST_MODE_IS);
  old_ptr = input_line_pointer;

  if (str == NULL) {
    as_bad(_("syntax error"));
    input_line_pointer = old_ptr;
    return;
  }

  p = skip_space(str);

  while (i < (size_t)BUFLEN && (ISALPHA(*p) || ISDIGIT(*p))) {
    buf[i++] = TOLOWER(*p++);
  }

  if (i == (size_t)BUFLEN) {
    buf[BUFLEN - 3] = '.';
    buf[BUFLEN - 2] = '.';
    buf[BUFLEN - 1] = 0;
    as_bad(_("Unknown instruction '%s'"), buf);
    input_line_pointer = old_ptr;
    return;
  }

  buf[i] = 0;

  dwarf2_emit_insn(0);

  if ((*p != '\0') && !is_whitespace(*p)) {
    if (*p != '.' || !(ins_ok & INS_EZ80) || !assemble_suffix(&p)) {
      as_bad(_("syntax error"));
      input_line_pointer = old_ptr;
      return;
    }
  }

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

  if ((!err_flag) && *p) {
    as_bad(_("junk at end of line, first unrecognized character is `%c'"), *p);
  }

  input_line_pointer = old_ptr;
}

#include <limits.h>

static int
signed_overflow(signed long value, unsigned bitsize)
{
    if (bitsize == 0) {
        return 1;
    }

    unsigned total_bits_long = (unsigned)(sizeof(signed long) * CHAR_BIT);
    if (bitsize >= total_bits_long) {
        return 0;
    }

    unsigned shift = bitsize - 1U;
    unsigned long umax = (1UL << shift) - 1UL;
    signed long max = (signed long)umax;
    signed long min = -max - 1;

    return (value < min) || (value > max);
}

#include <limits.h>

static int
unsigned_overflow(unsigned long value, unsigned bitsize)
{
    const unsigned word_bits = (unsigned)(sizeof(unsigned long) * CHAR_BIT);

    if (bitsize == 0) {
        return value != 0UL;
    }

    if (bitsize >= word_bits) {
        return 0;
    }

    return (value >> bitsize) != 0UL;
}

static int
is_overflow(long value, unsigned int bitsize)
{
    return (value < 0) ? signed_overflow(value, bitsize)
                       : unsigned_overflow(value, bitsize);
}

static inline void write_byte_at(char **p, long v, unsigned shift)
{
  *(*p)++ = (char)((v >> shift) & 0xFF);
}

static inline void write_le_bytes(char **p, long v, unsigned nbytes)
{
  for (unsigned i = 0; i < nbytes; ++i)
    write_byte_at(p, v, i * 8);
}

static inline void write_be_bytes(char **p, long v, unsigned nbytes)
{
  while (nbytes-- > 0)
    write_byte_at(p, v, nbytes * 8);
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
      write_byte_at(&p_lit, val, 0);
      break;

    case BFD_RELOC_Z80_BYTE0:
      write_byte_at(&p_lit, val, 0);
      break;

    case BFD_RELOC_Z80_BYTE1:
      write_byte_at(&p_lit, val, 8);
      break;

    case BFD_RELOC_Z80_BYTE2:
      write_byte_at(&p_lit, val, 16);
      break;

    case BFD_RELOC_Z80_BYTE3:
      write_byte_at(&p_lit, val, 24);
      break;

    case BFD_RELOC_8:
      if (fixP->fx_done && is_overflow (val, 8))
        as_warn_where (fixP->fx_file, fixP->fx_line,
                       _("8-bit overflow (%+ld)"), val);
      write_byte_at(&p_lit, val, 0);
      break;

    case BFD_RELOC_Z80_WORD1:
      write_byte_at(&p_lit, val, 16);
      write_byte_at(&p_lit, val, 24);
      break;

    case BFD_RELOC_Z80_WORD0:
      write_le_bytes(&p_lit, val, 2);
      break;

    case BFD_RELOC_16:
      if (fixP->fx_done && is_overflow (val, 16))
        as_warn_where (fixP->fx_file, fixP->fx_line,
                       _("16-bit overflow (%+ld)"), val);
      write_le_bytes(&p_lit, val, 2);
      break;

    case BFD_RELOC_24:
      if (fixP->fx_done && is_overflow (val, 24))
        as_warn_where (fixP->fx_file, fixP->fx_line,
                       _("24-bit overflow (%+ld)"), val);
      write_le_bytes(&p_lit, val, 3);
      break;

    case BFD_RELOC_32:
      if (fixP->fx_done && is_overflow (val, 32))
        as_warn_where (fixP->fx_file, fixP->fx_line,
                       _("32-bit overflow (%+ld)"), val);
      write_le_bytes(&p_lit, val, 4);
      break;

    case BFD_RELOC_Z80_16_BE:
      write_be_bytes(&p_lit, val, 2);
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

  if (bfd_reloc_type_lookup (stdoutput, fixp->fx_r_type) == NULL)
    {
      as_bad_where (fixp->fx_file, fixp->fx_line,
                    _("reloc %d not supported by object file format"),
                    (int) fixp->fx_r_type);
      return NULL;
    }

  reloc = notes_alloc (sizeof (*reloc));
  if (reloc == NULL)
    return NULL;

  reloc->sym_ptr_ptr = notes_alloc (sizeof (asymbol *));
  if (reloc->sym_ptr_ptr == NULL)
    return NULL;

  *reloc->sym_ptr_ptr = symbol_get_bfdsym (fixp->fx_addsy);

  if (fixp->fx_r_type == BFD_RELOC_VTABLE_INHERIT
      || fixp->fx_r_type == BFD_RELOC_VTABLE_ENTRY)
    reloc->address = fixp->fx_offset;
  else
    reloc->address = fixp->fx_frag->fr_address + fixp->fx_where;

  reloc->addend = fixp->fx_offset;
  reloc->howto = bfd_reloc_type_lookup (stdoutput, fixp->fx_r_type);

  return reloc;
}

int z80_tc_labels_without_colon(void)
{
    return colonless_labels;
}

int z80_tc_label_is_local(const char *name)
{
  const char *prefix = local_label_prefix;
  if (prefix == NULL || name == NULL)
    return 0;

  while (*prefix)
  {
    if (*name == '\0' || *name != *prefix)
      return 0;
    ++name;
    ++prefix;
  }
  return 1;
}

/* Parse floating point number from string and compute mantissa and
   exponent. Mantissa is normalized.
*/
#define EXP_MIN -0x10000
#define EXP_MAX 0x10000
static int
str_to_broken_float (bool *signP, uint64_t *mantissaP, int *expP)
{
  if (!signP || !mantissaP || !expP)
    return 0;

  const char *p = (const char *) skip_space (input_line_pointer);
  bool sign = (*p == '-');
  uint64_t mantissa = 0;
  int exponent = 0;

  *signP = sign;
  if (sign || *p == '+')
    ++p;

  if (strncasecmp (p, "NaN", 3) == 0)
    {
      *mantissaP = 0;
      *expP = 0;
      input_line_pointer = (char *) (p + 3);
      return 1;
    }
  if (strncasecmp (p, "inf", 3) == 0)
    {
      *mantissaP = 1ull << 63;
      *expP = EXP_MAX;
      input_line_pointer = (char *) (p + 3);
      return 1;
    }

  /* integral part */
  for (; ISDIGIT (*p); ++p)
    {
      if (mantissa >> 60)
        {
          if (*p >= '5')
            mantissa++;
          break;
        }
      mantissa = mantissa * 10 + (uint64_t) (*p - '0');
    }

  /* skip remaining integral digits and count them as exponent */
  for (; ISDIGIT (*p); ++p)
    exponent++;

  /* fractional part */
  if (*p == '.')
    {
      ++p;
      if (!exponent)
        {
          for (; ISDIGIT (*p); ++p, --exponent)
            {
              if (mantissa >> 60)
                {
                  if (*p >= '5')
                    mantissa++;
                  break;
                }
              mantissa = mantissa * 10 + (uint64_t) (*p - '0');
            }
        }
      for (; ISDIGIT (*p); ++p)
        ;
    }

  /* exponent part */
  if (*p == 'e' || *p == 'E')
    {
      int t = 0;
      ++p;
      bool es = (*p == '-');
      if (es || *p == '+')
        ++p;
      for (; ISDIGIT (*p); ++p)
        {
          if (t < 100)
            t = t * 10 + (*p - '0');
        }
      exponent += es ? -t : t;
    }

  if (ISALNUM (*p) || *p == '.')
    return 0;

  input_line_pointer = (char *) p;

  if (mantissa == 0)
    {
      *mantissaP = 1ull << 63;
      *expP = EXP_MIN;
      return 1;
    }

  {
    const uint64_t U64_MAX_DIV10 = ~0ull / 10;

    /* decimal normalization */
    while (mantissa <= U64_MAX_DIV10)
      {
        mantissa *= 10;
        --exponent;
      }

    /* convert decimal exponent to binary exponent */
    int i = 64;

    while (exponent > 0)
      {
        while (mantissa > U64_MAX_DIV10)
          {
            mantissa >>= 1;
            i += 1;
          }
        mantissa *= 10;
        --exponent;
      }

    while (exponent < 0)
      {
        while (!(mantissa >> 63))
          {
            mantissa <<= 1;
            i -= 1;
          }
        mantissa /= 10;
        ++exponent;
      }

    while (!(mantissa >> 63))
      {
        mantissa <<= 1;
        --i;
      }

    *mantissaP = mantissa;
    *expP = i;
  }

  return 1;
}

static const char *
str_to_zeda32(char *litP, int *sizeP)
{
  if (litP == NULL || sizeP == NULL)
    return _("invalid syntax");

  uint64_t mantissa = 0;
  bool sign = false;
  int exponent = 0;

  *sizeP = 4;

  if (!str_to_broken_float(&sign, &mantissa, &exponent))
    return _("invalid syntax");

  exponent -= 1;

  mantissa >>= 39;
  mantissa += 1;
  mantissa >>= 1;

  if (mantissa >> 24)
    {
      mantissa >>= 1;
      ++exponent;
    }

  if (exponent < -127)
    {
      exponent = -128;
      mantissa = 0;
    }
  else if (exponent > 127)
    {
      exponent = -128;
      mantissa = sign ? 0xc00000u : 0x400000u;
    }
  else if (mantissa == 0)
    {
      exponent = -128;
      mantissa = 0x200000u;
    }
  else if (!sign)
    {
      mantissa &= ((uint64_t)1 << 23) - 1;
    }

  unsigned char *out = (unsigned char *)litP;
  out[0] = (unsigned char)((mantissa >> 0) & 0xFFu);
  out[1] = (unsigned char)((mantissa >> 8) & 0xFFu);
  out[2] = (unsigned char)((mantissa >> 16) & 0xFFu);
  out[3] = (unsigned char)(0x80 + exponent);

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
  enum { FLOAT48_SIZE = 6, MANTISSA_BITS = 40, ROUND_SHIFT = 23, EXP_MIN = -127, EXP_MAX = 127 };

  uint64_t mantissa = 0;
  bool sign = false;
  int exponent = 0;

  if (!litP || !sizeP)
    return _("invalid syntax");

  *sizeP = FLOAT48_SIZE;

  if (!str_to_broken_float(&sign, &mantissa, &exponent))
    return _("invalid syntax");

  mantissa >>= ROUND_SHIFT;
  ++mantissa;
  mantissa >>= 1;

  if (mantissa >> MANTISSA_BITS)
    {
      mantissa >>= 1;
      ++exponent;
    }

  if (exponent < EXP_MIN)
    {
      uint8_t zero[FLOAT48_SIZE] = { 0 };
      memcpy(litP, zero, FLOAT48_SIZE);
      return NULL;
    }

  if (exponent > EXP_MAX)
    return _("overflow");

  if (!sign)
    {
      const uint64_t mask = (1ULL << (MANTISSA_BITS - 1)) - 1ULL;
      mantissa &= mask;
    }

  {
    uint8_t buf[FLOAT48_SIZE];
    buf[0] = (uint8_t)(0x80 + exponent);
    for (unsigned i = 0, idx = 1; i < MANTISSA_BITS; i += 8, ++idx)
      buf[idx] = (uint8_t)((mantissa >> i) & 0xFFu);
    memcpy(litP, buf, FLOAT48_SIZE);
  }

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
  return ieee_md_atof('s', litP, sizeP, 0);
}

static const char *
str_to_ieee754_d(char *litP, int *sizeP)
{
    if (litP == NULL || sizeP == NULL) {
        return NULL;
    }
    return ieee_md_atof('d', litP, sizeP, 0);
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
int z80_dwarf2_addr_size(const bfd *abfd)
{
  if (abfd == NULL)
    return 2;

  long mach = bfd_get_mach(abfd);
  return (mach == bfd_mach_ez80_adl) ? 3 : 2;
}
