/**
 * \file flt_livefilter.c
 * \author Christian Kruse, <ckruse@wwwtech.de>
 *
 * Live category and poster filter
 */

/* {{{ Initial comments */
/*
 * $LastChangedDate$
 * $LastChangedRevision$
 * $LastChangedBy$
 *
 */
/* }}} */

/* {{{ Includes */
#include "config.h"
#include "defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>

#include "readline.h"
#include "hashlib.h"
#include "utils.h"
#include "configparser.h"
#include "cfcgi.h"
#include "template.h"
#include "clientlib.h"
#include "charconvert.h"

/* }}} */

static const u_char *symbols[] = {
  "TOK_EOS", "TOK_EQ", "TOK_NE",
  "TOK_OR", "TOK_AND", "TOK_ID",
  "TOK_STR", "TOK_CONTAINS", "TOK_LPAREN",
  "TOK_RPAREN"
};

#define flt_lf_get_type(x) (symbols[x])

#define PREC_ID    60
#define PREC_EQ    50 /* =, != */
#define PREC_AND   40
#define PREC_OR    30
#define PREC_PAREN 20

#define TOK_EOS          0x00
#define TOK_EQ           0x01
#define TOK_NE           0x02
#define TOK_OR           0x03
#define TOK_AND          0x04
#define TOK_ID           0x05
#define TOK_STR          0x06
#define TOK_CONTAINS     0x07
#define TOK_LPAREN       0x08
#define TOK_RPAREN       0x09
#define TOK_CONTAINS_NOT 0x0A

typedef struct s_node {
  int type,prec;
  u_char *content;

  struct s_node *left,*right,*parent,*argument;
} t_flt_lf_node;

static t_flt_lf_node *flt_lf_first = NULL;
static int flt_lf_active = 0;

static t_string flt_lf_str = { 0, 0, NULL };

u_char *case_strstr(const u_char *haystack,const u_char *needle) {
  size_t len1 = strlen(haystack);
  size_t len2 = strlen(needle);
  size_t i;

  if(len1 < len2) return NULL;
  if(len1 == len2) return cf_strcasecmp(haystack,needle) == 0 ? (u_char *)haystack : NULL;

  for(i=0;i<=len1-len2;i++) {
    if(cf_strncasecmp(haystack+i,needle,len2) == 0) return (u_char *)(haystack+i);
  }

  return NULL;
}

int read_string(register u_char *ptr) {
  str_init(&flt_lf_str);

  for(;*ptr;ptr++) {
    switch(*ptr) {
      case '\\':
        switch(*(++ptr)) {
          case 'n':
            str_char_append(&flt_lf_str,'\n');
            break;
          case 't':
            str_char_append(&flt_lf_str,'\t');
            break;
          case '"':
            str_char_append(&flt_lf_str,'"');
            break;
          default:
            str_char_append(&flt_lf_str,*ptr);
            break;
        }
      case '"':
        return 0;
      default:
        str_char_append(&flt_lf_str,*ptr);
    }
  }

  return -1;
}

int flt_lf_scanner(u_char *str,u_char **pos) {
  register u_char *ptr = *pos;
  int ret;

  for(;*ptr;ptr++) {
    /* go to the next token */
    for(;*ptr && isspace(*ptr);ptr++);

    *pos = ptr;

    /* decide, what it is */
    switch(*ptr) {
      case '=':
        if(*(ptr + 1) == '~') {
          *pos = ptr + 2;
          str_char_set(&flt_lf_str,ptr,2);
          return TOK_CONTAINS;
        }
        else {
          *pos = ptr + 1;
          str_char_set(&flt_lf_str,ptr,1);
          return TOK_EQ;
        }
      case '(':
        str_char_set(&flt_lf_str,ptr,1);
        *pos = ptr + 1;
        return TOK_LPAREN;
      case ')':
        str_char_set(&flt_lf_str,ptr,1);
        *pos = ptr + 1;
        return TOK_RPAREN;
      case '!':
        str_char_set(&flt_lf_str,ptr,2);
        *pos = ptr + 2;

        if(*(ptr + 1) == '~') {
          return TOK_CONTAINS_NOT;
        }
        else {
          return TOK_NE;
        }
      case '|':
        str_char_set(&flt_lf_str,ptr,1);
        *pos = ptr + 1;
        return TOK_OR;
      case '&':
        str_char_set(&flt_lf_str,ptr,1);
        *pos = ptr + 1;
        return TOK_AND;
      case '"':
        /* read string */
        if((ret = read_string(ptr+1)) != 0) return ret;

        /* safe new position */
        *pos = ptr + flt_lf_str.len + 2;

        /* return token */
        return TOK_STR;
      default:
        if(*ptr) {
          /* find end */
          for(;*ptr && isalnum(*ptr);ptr++);

          /* safe identifier */
          str_char_set(&flt_lf_str,*pos,ptr - *pos);

          /* set position */
          *pos = ptr;

          /* return token */
          return TOK_ID;
        }
        else {
          return TOK_EOS;
        }
    }
  }

  return TOK_EOS;
}

t_flt_lf_node *flt_lf_insert_node(t_flt_lf_node *cur,t_flt_lf_node *tok,t_flt_lf_node *root) {
  t_flt_lf_node *n = cur;

  if(!cur) {
    if(root) root->argument = tok;
    else     flt_lf_first = tok;
    return tok;
  }

  if(cur->prec <= tok->prec) {
    tok->parent = cur;

    if(cur->left) {
      cur->right = tok;
    }
    else {
      cur->left = tok;
    }
  }
  else {
    /* we have to search the right position due to the precedence */
    while(cur && cur->prec > tok->prec) {
      n   = cur;
      cur = cur->parent;
    }

    if(!cur) {
      if(root) root->argument = tok;
      else     flt_lf_first = tok;
      tok->left         = n;
      tok->left->parent = tok;
    }
    else {
      tok->parent = cur;

      if(cur->right) {
        tok->left         = cur->right;
        tok->left->parent = tok;
        cur->right        = tok;
      }
      else {
        tok->left         = cur->left;
        tok->left->parent = tok;
        cur->left         = tok;
      }
    }
  }

  return tok;
}

int flt_lf_parse_string(u_char *str,u_char **pos,t_cf_template *tpl,t_flt_lf_node *node,t_flt_lf_node *root_node,t_configuration *dc) {
  int ret = 0;
  t_flt_lf_node *current = NULL;
  t_name_value *cs = cfg_get_first_value(dc,"ExternCharset");

  while((ret = flt_lf_scanner(str,pos)) > 0) {
    current       = fo_alloc(NULL,1,sizeof(*current),FO_ALLOC_CALLOC);
    current->type = ret;

    switch(ret) {
      case TOK_STR:
      case TOK_ID:
        current->prec = PREC_ID;

        if(cf_strcasecmp(flt_lf_str.content,"true") == 0 && ret == TOK_ID) {
          current->content = (u_char *)1;
          str_cleanup(&flt_lf_str);
        }
        else if(cf_strcasecmp(flt_lf_str.content,"false") == 0 && ret == TOK_ID) {
          current->content = (u_char *)0;
          str_cleanup(&flt_lf_str);
        }
        else {
          if(cf_strcmp(cs->values[0],"UTF-8")) {
            if((current->content = charset_convert(flt_lf_str.content,flt_lf_str.len,cs->values[0],"UTF-8",NULL)) == NULL) {
              current->content = flt_lf_str.content;
              str_init(&flt_lf_str);
            }
            else {
              str_cleanup(&flt_lf_str);
            }
          }
          else {
            current->content = flt_lf_str.content;
            str_init(&flt_lf_str);
          }
        }

        break;
      case TOK_LPAREN:
        current->prec     = PREC_PAREN;
        current->argument = fo_alloc(NULL,1,sizeof(*current->argument),FO_ALLOC_CALLOC);
        if((ret = flt_lf_parse_string(str,pos,tpl,NULL,current,dc)) != 0) return ret;
        break;
      case TOK_RPAREN:
        free(current);
        return 0;
        break;
      case TOK_CONTAINS_NOT:
      case TOK_CONTAINS:
      case TOK_NE:
      case TOK_EQ:
        current->prec = PREC_EQ;
        break;
      case TOK_OR:
        current->prec = PREC_OR;
        break;
      case TOK_AND:
        current->prec = PREC_AND;
        break;
    }

    node = flt_lf_insert_node(node,current,root_node);
  }

  return 0;
}

int flt_lf_form(t_cf_hash *head,t_configuration *dc,t_configuration *vc,t_cf_template *begin,t_cf_template *end) {
  u_char *filter_str,*pos;

  if(flt_lf_active) {
    tpl_cf_setvar(begin,"livefilter","1",1,0);

    if(head) {
      if((filter_str = cf_cgi_get(head,"lf")) != NULL) {
        pos = filter_str;
        flt_lf_parse_string(filter_str,&pos,begin,NULL,NULL,dc);
        tpl_cf_setvar(begin,"lf",filter_str,strlen(filter_str),1);
      }
    }

    return FLT_OK;
  }

  return FLT_DECLINE;
}

u_char *flt_lf_evaluate(t_flt_lf_node *n,t_message *msg,u_int64_t tid) {
  u_char *l = NULL,*r = NULL;
  u_char buff[50];
  u_char *ret = NULL;
  t_mod_api is_visited = cf_get_mod_api_ent("is_visited");

  if(!n) return NULL;

  switch(n->type) {
    case TOK_CONTAINS:
      l = flt_lf_evaluate(n->left,msg,tid);
      r = flt_lf_evaluate(n->right,msg,tid);

      if(l == NULL && r == NULL) {
        ret = (u_char *)1;
      }
      else {
        if(l == (u_char *)1 && r == (u_char *)1) {
          ret = (u_char *)1;
        }
        else {
          if(l == NULL || r == NULL || l == (u_char *)1 || r == (u_char *)1) {
            ret = NULL;
          }
          else {
            if(case_strstr(l,r)) {
              ret = (u_char *)1;
            }
            else {
              ret = NULL;
            }
          }
        }
      }

      return ret;

    case TOK_CONTAINS_NOT:
      l = flt_lf_evaluate(n->left,msg,tid);
      r = flt_lf_evaluate(n->right,msg,tid);

      if(l == NULL && r == NULL) {
        ret = NULL;
      }
      else {
        if(l == (u_char *)1 && r == (u_char *)1) {
          ret = NULL;
        }
        else {
          if(l == NULL || r == NULL || l == (u_char *)1 || r == (u_char *)1) {
            ret = (u_char *)1;
          }
          else {
            if(case_strstr(l,r)) {
              ret = NULL;
            }
            else {
              ret = (u_char *)1;
            }
          }
        }
      }

      return ret;

    case TOK_EQ:
      l = flt_lf_evaluate(n->left,msg,tid);
      r = flt_lf_evaluate(n->right,msg,tid);

      if(l == NULL && r == NULL) {
        ret = (u_char *)1; /* true: both undefined */
      }
      else {
        if(l == (u_char *)1 && r == (u_char *)1) {
          ret = (u_char *)1; /* true: both true */
        }
        else {
          if(!l || !r || l == (u_char *)1 || r == (u_char *)1) {
            ret = NULL;      /* false: only one of them has this value */
          }
          else {
            if(cf_strcasecmp(l,r) == 0) {
              ret = (u_char *)1; /* true: strings are equal */
            }
            else {
              ret = NULL;
            }
          }
        }
      }

      if(l > (u_char *)1) free(l);
      if(r > (u_char *)1) free(r);

      return ret;

    case TOK_NE:
      l = flt_lf_evaluate(n->left,msg,tid);
      r = flt_lf_evaluate(n->right,msg,tid);

      if(l == NULL && r == NULL) {
        ret = NULL;      /* false: both are undefined */
      }
      else {
        if(l == (u_char *)1 && r == (u_char *)1) {
          ret = NULL;      /* false: both are true */
        }
        else {
          if(!l || !r || l == (u_char *)1 || r == (u_char *)1) {
            ret = (u_char *)1; /* true: only one of them has this value */
          }
          else {
            if(cf_strcasecmp(l,r) == 0) {
              ret = NULL;      /* false: strings are equal */
            }
            else {
              ret = (u_char *)1;
            }
          }
        }
      }

      if(l > (u_char *)1) free(l);
      if(r > (u_char *)1) free(r);

      return ret;

    case TOK_OR:
      l = flt_lf_evaluate(n->left,msg,tid);
      if(l) ret = (u_char *)1;

      if(!ret) {
        r = flt_lf_evaluate(n->right,msg,tid);
        if(r) ret = (u_char *)1;
      }

      if(l > (u_char *)1) free(l);
      if(r > (u_char *)1) free(r);

      return ret;

    case TOK_AND:
      l = flt_lf_evaluate(n->left,msg,tid);
      r = flt_lf_evaluate(n->right,msg,tid);

      if(!l && !r) {
        ret = (u_char *)1; /* true: both are undefined */
      }
      else {
        if(r == (u_char *)1 && l == (u_char *)1) {
          ret = (u_char *)1;
        }
        else {
          if(!l || !r || r == (u_char *)1 || l == (u_char *)1) {
            ret = NULL;      /* false: only one undefined */
          }
          else {
            if(cf_strcasecmp(r,l) == 0) {
              ret = (char *)1; /* true: strings are equal */
            }
            else {
              ret = NULL;
            }
          }
        }
      }

      if(l > (u_char *)1) free(l);
      if(r > (u_char *)1) free(r);

      return ret;

    case TOK_ID:
      if(n->content > (u_char *)1) {
        switch(*n->content) {
          case 'm':
            snprintf(buff,50,"%lld",msg->mid);
            return strdup(buff);
          case 't':
            snprintf(buff,50,"%lld",tid);
            return strdup(buff);
          case 'a':
            return strdup(msg->author);
          case 's':
            return strdup(msg->subject);
          case 'c':
            return strdup(msg->category);
          case 'd':
            snprintf(buff,50,"%ld",msg->date);
            return strdup(buff);
          case 'l':
            snprintf(buff,50,"%d",msg->level);
            return strdup(buff);
          case 'v':
            if(cf_strcasecmp(n->content,"visited") == 0) {
              if(is_visited) {
                return (u_char *)(is_visited(&(msg->mid)) == NULL ? 0 : 1);
              }
              else {
                return NULL;
              }
            }
            else {
              return (u_char *)(msg->may_show == 0 ? 0 : 1);
            }
        }
      }
      else {
        return (u_char *)(n->content == (u_char *)1 ? 1 : 0);
      }
    case TOK_STR:
      return strdup(n->content);
    case TOK_LPAREN:
      if(n->argument) {
        ret = flt_lf_evaluate(n->argument,msg,tid);
      }
      else ret = NULL;
      return ret;
  }

  return NULL;
}

int flt_lf_filter(t_cf_hash *head,t_configuration *dc,t_configuration *vc,t_message *msg,u_int64_t tid,int mode) {
  if(flt_lf_active) {
    if(flt_lf_first) {
      if(flt_lf_evaluate(flt_lf_first,msg,tid) == 0) {
        msg->may_show = 0;
      }
    }

    return FLT_OK;
  }

  return FLT_DECLINE;
}

int flt_lf_handle_command(t_configfile *cf,t_conf_opt *opt,u_char **args,int argnum) {
  flt_lf_active = cf_strcmp(args[0],"yes") == 0;
  return 0;
}

void flt_lf_cleanup(void) {
  /* \todo Implement cleanup code (I'm a bad guy) */
}

t_conf_opt config[] = {
  { "ActivateLiveFilter", flt_lf_handle_command, NULL },
  { NULL, NULL, NULL }
};

t_handler_config handlers[] = {
  { VIEW_INIT_HANDLER, flt_lf_form },
  { VIEW_LIST_HANDLER, flt_lf_filter },
  { 0, NULL }
};

t_module_config flt_livefilter = {
  config,
  handlers,
  NULL,
  NULL,
  NULL,
  flt_lf_cleanup
};

/* eof */