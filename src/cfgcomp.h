/**
 * \file configlexer.c
 * \author Christian Kruse, <ckruse@wwwtech.de>
 *
 * This file contains the configuration parser interface
 */

#ifndef _CF_CONFIG_H
#define _CF_CONFIG_H

#define CF_NO_TOK       0x0
#define CF_TOK_DQ       0x1
#define CF_TOK_SQ       0x2
#define CF_TOK_LPAREN   0x3
#define CF_TOK_RPAREN   0x4
#define CF_TOK_DOT      0x5
#define CF_TOK_PLUS     0x6
#define CF_TOK_MINUS    0x7
#define CF_TOK_DIV      0x8
#define CF_TOK_MULT     0x9
#define CF_TOK_SEMI     0xA
#define CF_TOK_PERC     0xB
#define CF_TOK_EQ       0xC
#define CF_TOK_SET      0xD
#define CF_TOK_NOTEQ    0xE
#define CF_TOK_NOT      0xF
#define CF_TOK_LTEQ     0x10
#define CF_TOK_LT       0x11
#define CF_TOK_GTEQ     0x12
#define CF_TOK_GT       0x13
#define CF_TOK_NUM      0x14
#define CF_TOK_IF       0x15
#define CF_TOK_ELSEIF   0x16
#define CF_TOK_ELSE     0x17
#define CF_TOK_WITH     0x18
#define CF_TOK_END      0x19
#define CF_TOK_AND      0x1A
#define CF_TOK_OR       0x1B
#define CF_TOK_IDENT    0x1C
#define CF_TOK_DOLLAR   0x1D
#define CF_TOK_EOF      0x1E
#define CF_TOK_STRING   0x1F
#define CF_TOK_COMMA    0x20
#define CF_TOK_ARRAY    0x21
#define CF_TOK_STMT     0x22
#define CF_TOK_LOAD     0x23
#define CF_TOK_RBRACKET 0x24
#define CF_TOK_LBRACKET 0x25
#define CF_TOK_FID      0x26
#define CF_TOK_COMMARPAREN 0x27
#define CF_TOK_NEXTTOK  0x28

#define CF_TOK_IFELSEELSEIF 0x21

#define CF_RETVAL_OK               0
#define CF_RETVAL_NOSUCHFILE      -1
#define CF_RETVAL_FILENOTREADABLE -2
#define CF_RETVAL_PARSEERROR      -3

#define CF_PREC_ATOM        100
#define CF_PREC_PT           90
#define CF_PREC_BR           83
#define CF_PREC_NOT          82
#define CF_PREC_EQ           80
#define CF_PREC_LTGT         70
#define CF_PREC_PLUSMINUS    60
#define CF_PREC_DIVMUL       50
#define CF_PREC_AND          40
#define CF_PREC_OR           30
#define CF_PREC_SET          10

#define CF_TYPE_INT 0x1
#define CF_TYPE_STR 0x2
#define CF_TYPE_ID  0x3


#define CF_ASM_MODULE    0x1
#define CF_ASM_PUSH      0x2
#define CF_ASM_PUSHDIR   0x3
#define CF_ASM_UNSET     0x4
#define CF_ASM_LOAD      0x5

#define CF_ASM_CPY       0x6
#define CF_ASM_EQ        0x7
#define CF_ASM_NE        0x8
#define CF_ASM_LT        0x9
#define CF_ASM_LTEQ      0xA
#define CF_ASM_GT        0xB
#define CF_ASM_GTEQ      0xC
#define CF_ASM_ADD       0xD
#define CF_ASM_SUB       0xE
#define CF_ASM_DIV       0xF
#define CF_ASM_MUL       0x10
#define CF_ASM_JMP       0x11
#define CF_ASM_JMPIF     0x12
#define CF_ASM_JMPIFNOT  0x13
#define CF_ASM_AND       0x14
#define CF_ASM_OR        0x15
#define CF_ASM_NEG       0x16
#define CF_ASM_ARRAY     0x17
#define CF_ASM_ARRAYSUBS 0x18
#define CF_ASM_ARRAYPUSH 0x19

#define CF_ASM_ARG_REG 0x1
#define CF_ASM_ARG_NUM 0x2
#define CF_ASM_ARG_STR (0x1|0x2)

#define CF_ASM_T_STR  0x1
#define CF_ASM_T_ATOM 0x2
#define CF_ASM_T_LBL  0x3

typedef struct {
  int type;
  u_char *val;
} cf_cfg_asm_tok_t;

typedef struct {
  u_char *name;
  u_int32_t lbl_off;
  u_int32_t *repl_offs;
  int rlen;
} cf_asm_replacements_t;

typedef struct cf_cfg_value_s {
  int type,ival;
  u_char *sval;
} cf_cfg_value_t;


typedef struct cf_cfg_trees_s cf_cfg_trees_t;

typedef struct cf_cfg_token_s {
  int type,prec,line;
  cf_cfg_value_t *data;

  cf_cfg_trees_t *arguments;
  size_t arglen;

  struct cf_cfg_token_s *left,*right,*parent;
} cf_cfg_token_t;

struct cf_cfg_trees_s {
  int type,arglen;
  void *data;

  cf_cfg_token_t *tree;

  struct cf_cfg_trees_s **arguments;
  struct cf_cfg_trees_s *next,*prev;
};

typedef struct {
  u_char *content,*pos;
  int line;

  int numtok;
  u_char *stok;

  u_char *modpath;

  cf_cfg_trees_t *trees;
} cf_cfg_stream_t;

typedef struct {
  char registers;
  int lbls_used;
} cf_cfg_vmstate_t;


int cf_cfg_lexer(cf_cfg_stream_t *stream,int changestate);
u_char *cf_dbg_get_token(int ttype);

int cf_cfg_parser(cf_cfg_stream_t *stream,cf_cfg_trees_t *exec,cf_cfg_token_t *cur,int level,int retat);
int cf_cfg_codegenerator(cf_cfg_stream_t *stream,cf_cfg_trees_t *trees,cf_cfg_vmstate_t *state,cf_string_t *str,int regarg);

void cf_cfg_parser_destroy_tokens(cf_cfg_token_t *tok);
void cf_cfg_parser_destroy_stream(cf_cfg_stream_t *stream);
void cf_cfg_parser_destroy_trees(cf_cfg_trees_t *tree);

#endif
