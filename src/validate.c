/**
 * \file validate.c
 * \author Christian Kruse, <ckruse@wwwtech.de>
 * \brief Validation functions
 *
 * This file contains validation functions (e.g. link and mail validation)
 *
 * \todo file:// URL validation
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <idna.h>

#include "utils.h"
#include "validate.h"
/* }}} */

static int is_valid_http_link_check(const u_char *link);

/* {{{ scheme_list */
static const t_scheme_list scheme_list[] = {
  { "http",     is_valid_http_link_check },
  { "https",    is_valid_http_link_check },
  { "mailto",   is_valid_mailto_link },
  { "wais",     is_valid_wais_link },
  { "prospero", is_valid_prospero_link },
  { "telnet",   is_valid_telnet_link },
  { "nntp",     is_valid_nntp_link },
  { "news",     is_valid_news_link },
  { "gopher",   is_valid_gopher_link },
  { "\0", NULL }
};
/* }}} */

/* {{{ isextra */
static int isextra(u_char c) {
  switch(c) {
    case '!':
    case '*':
    case '\'':
    case '(':
    case ')':
    case ',':
      return 1;
  }

  return 0;
}
/* }}} */

/* {{{ issafe */
static int issafe(u_char c) {
  switch(c) {
    case '$':
    case '-':
    case '_':
    case '.':
    case '+':
      return 1;
  }

  return 0;
}
/* }}} */

/* {{{ isuchar_wo_escape */
static int isuchar_wo_escape(u_char c) {
  /* escape is not checked! */
  if((!isalnum(c) || !isascii(c)) && !issafe(c) && !isextra(c)) return 0;
  return 1;
}
/* }}} */

/* {{{ is_valid_hostname */
int is_valid_hostname(const u_char *hostname) {
  register const u_char *ptr;
  u_char *out = NULL;
  int count = 0;

  for(ptr=hostname;*ptr;ptr++) {
    if(!isalnum(*ptr) || !isascii(*ptr)) {
      if(*ptr == '.') {
        ++count;
        /* two dots after another are not allowed, neither a dot at the beginning */
        if(ptr == hostname || *(ptr-1) == '.') return -1;
      }
      else continue;

      /* hu, we got a not-alnum ascii character. IDN? */
      if(idna_to_ascii_8z(hostname,(char **)&out,IDNA_USE_STD3_ASCII_RULES) != IDNA_SUCCESS) return -1;

      /* idna did the check for us; it's valid */
      free(out);
      return 0;
    }
  }

  /* ok, all characters are alnum and ascii or dots. Hostname is valid */
  return count == 0 ? -1 : 0;
}
/* }}} */

/* {{{ is_valid_http_link_check */
static int is_valid_http_link_check(const u_char *link) {
  /* we allow anchors by default */
  return is_valid_http_link(link,0);
}
/* }}} */

/* {{{ is_valid_http_link */
int is_valid_http_link(const u_char *link,int strict) {
  register const u_char *ptr;
  const u_char *begin = NULL,*end = NULL;
  u_char *helper;

  /* first we check if the scheme is valid */
  if(cf_strncmp(link,"http://",7)) {
    if(cf_strncmp(link,"https://",8)) return -1;

    ptr = link+8;
  }
  else ptr = link + 7;

  begin = ptr;

  /* ok, it seems as if scheme is valid -- get hostname */
  for(;*ptr;ptr++) {
    if(*ptr == ':' || *ptr == '/' || *ptr == '#' || *ptr == '?') {
      end = ptr-1;
      break;
    }
  }

  /* URL consists only of a hostname if end is NULL */
  if(end == NULL) {
    end = ptr-1;
  }

  helper = strndup(begin,end-begin+1);
  if(is_valid_hostname(helper) == -1) {
    /* ups, no valid hostname -- die */
    free(helper);
    return -1;
  }

  free(helper);

  /* hostname is valid; follows a port? */
  if(*ptr == ':') {
    /* port has to be digit+ */
    for(begin=++ptr;*ptr;ptr++) {
      /* port is valid (at least one digit followed by a slash) */
      if(*ptr == '/' && ptr > begin) break;

      /* hu? port must be digits... bad boy! */
      if(!isdigit(*ptr)) return -1;
    }
  }

  /* follows a host path? */
  if(*ptr == '/') {
    for(begin=ptr;*ptr;ptr++) {
      /* escape sequenz */
      if(*ptr == '%') {
        if(!isxdigit(*(ptr+1)) || !isxdigit(*(ptr+2))) return -1;
        ptr += 2;
        continue;
      }

      /* anchor means end of host path */
      if(*ptr == '#') break;

      if(!isuchar_wo_escape(*ptr)) {
        switch(*ptr) {
          case '/':
          case ';':
          case ':':
          case '@':
          case '&':
          case '=':
            continue;
          default:
            if(*ptr != '?') return -1;
        }

        /* could only be a question mark (end of host path) */
        break;
      }
    }
  }

  /* follows a search path? */
  if(*ptr == '?') {
    for(++ptr;*ptr;ptr++) {
      /* escaped character */
      if(*ptr == '%') {
        if(!isxdigit(*(ptr+1)) || !isxdigit(*(ptr+2))) return -1;
        ptr += 2;
        continue;
      }

      /* anchor means end of search path */
      if(*ptr == '#') break;

      /* we checked for escaped before */
      if(!isuchar_wo_escape(*ptr)) {
        switch(*ptr) {
          case ';':
          case ':':
          case '@':
          case '&':
          case '=':
            break;
          default:
            /* no anchor in strict mode */
            return -1;
        }
      }
    }
  }

  if(*ptr == '#') {
    if(strict) return -1;

    for(ptr++;*ptr;ptr++) {
      if(*ptr == '%') {
        if(!isxdigit(*(ptr+1)) || !isxdigit(*(ptr+2))) return -1;
        ptr += 2;
        continue;
      }

      if(!isuchar_wo_escape(*ptr)) {
        switch(*ptr) {
          case ';':
          case '/':
          case '?':
          case ':':
          case '@':
          case '&':
          case '=':
            break;
          default:
            return -1;
        }
      }
    }
  }

  return 0;
}
/* }}} */

/* {{{ is_valid_mailto_link */
int is_valid_mailto_link(const u_char *addr) {
  /* first, check if mail address begins with mailto: */
  if(cf_strncmp(addr,"mailto:",7) == 0) {
    /* let is_valid_mailaddress() do the checks... */
    return is_valid_mailaddress(addr+7);
  }

  /* not a mailto:-scheme -- bad guy! invalid! */
  return -1;
}
/* }}} */

/* {{{ is_valid_mailaddress */
/*
 * Algorithm is in parts from 'Secure Cooking With C and C++'
 */
int is_valid_mailaddress(const u_char *address) {
  register const u_char *c, *domain;
  static u_char *rfc822_specials = "()<>@,;:\\\"[]";

  /* first we validate the name portion (name@domain) */
  for(c=address;*c;c++) {
    if(*c == '\"' && (c == address || *(c - 1) == '.' || *(c - 1) == '\"')) {
      while (*++c) {
        if (*c == '\"') break;
        if (*c == '\\' && (*++c == ' ')) continue;
        if (*c <= ' ' || *c >= 127) return -1;
      }

      if(!*c++) return -1;
      if(*c == '@') break;
      if(*c != '.') return -1;
      continue;
    }

    if(*c == '@') break;
    if(*c <= ' ' || *c >= 127) return -1;
    if(strchr(rfc822_specials, *c)) return -1;
  }

  if(c == address || *(c - 1) == '.') return -1;

  /* next we validate the domain portion (name@domain) */
  if (!*(domain = ++c)) return -1;

  /*
   * we also have to accept Umlauts domains, which means we have
   * to check domain name more complicated than the secure cooking
   * does
   */
  return is_valid_hostname(domain);
}
/* }}} */

/* {{{ is_valid_wais_link */
int is_valid_wais_link(const u_char *link) {
  register const u_char *ptr;
  const u_char *begin = NULL,*end = NULL;
  u_char *helper;
  unsigned int slashes;

  /* first we check if the scheme is valid */
  if(cf_strncmp(link,"wais://",7)) {
    return -1;
  }
  else ptr = (u_char *)link + 7;

  begin = ptr;

  /* ok, it seems as if scheme is valid -- get hostname */
  for(;*ptr;ptr++) {
    if(*ptr == ':' || *ptr == '/' || *ptr == '?') {
      end = ptr-1;
      break;
    }
  }

  /* URL consists only of a hostname if end is NULL */
  if(end == NULL) {
    end = ptr-1;
  }

  helper = strndup(begin,end-begin+1);
  if(is_valid_hostname(helper) == -1) {
    /* ups, no valid hostname -- die */
    free(helper);
    return -1;
  }

  free(helper);

  /* hostname is valid; follows a port? */
  if(*ptr == ':') {
    /* port has to be digit+ */
    for(begin=++ptr;*ptr;ptr++) {
      /* port is valid (at least one digit followed by a slash) */
      if(*ptr == '/' && ptr > begin) break;

      /* hu? port must be digits... bad boy! */
      if(!isdigit(*ptr)) return -1;
    }
  }

  /* does not follow host path, no wais url*/
  if(*ptr++ != '/') return -1;
  
  slashes = 0;
  for(begin=ptr;*ptr;ptr++) {
    /* escape sequenz */
    if(*ptr == '%') {
      if(!isxdigit(*(ptr+1)) || !isxdigit(*(ptr+2))) return -1;
      ptr += 2;
      continue;
    }
    
  
    if(!isuchar_wo_escape(*ptr)) {
      switch(*ptr) {
        case '/':
          slashes++;
          if(slashes > 2) return -1;
          continue;
        default:
          if(*ptr != '?' || slashes) return -1;
      }
      /* could only be a question mark (end of host path) */
      break;
    }
  }
  
  /* three forms of wais url
     1. wais://hostport/database   (slashes == 0, !*ptr)
     2. wais://hostport/database/wtype/wpath (slashes == 2, !*ptr)
     3. wais://hostport/database?search (slashes == 0, *ptr == '?')
   */
  if(slashes && (slashes != 2 || *ptr)) return -1;

  /* follows a search path? */
  if(*ptr == '?') {
    for(++ptr;*ptr;ptr++) {
      /* escaped character */
      if(*ptr == '%') {
        if(!isxdigit(*(ptr+1)) || !isxdigit(*(ptr+2))) return -1;
        ptr += 2;
        continue;
      }

      /* we checked for escaped before */
      if(!isuchar_wo_escape(*ptr)) {
        switch(*ptr) {
          case ';':
          case ':':
          case '@':
          case '&':
          case '=':
            break;
          default:
            /* no anchor in strict mode */
            return -1;
        }
      }
    }
  }
  
  /* now url should definitely be at an end! */
  if (*ptr) return -1;
  
  return 0;

}
/* }}} */

/* {{{ is_valid_prospero_link */
int is_valid_prospero_link(const u_char *link) {
  register const u_char *ptr;
  const u_char *begin = NULL,*end = NULL;
  u_char *helper;
  int had_cur_equal = 0;

  /* first we check if the scheme is valid */
  if(cf_strncmp(link,"prospero://",11)) {
    return -1;
  }
  else ptr = link + 11;

  begin = ptr;

  /* ok, it seems as if scheme is valid -- get hostname */
  for(;*ptr;ptr++) {
    if(*ptr == ':' || *ptr == '/') {
      end = ptr-1;
      break;
    }
  }

  /* URL consists only of a hostname if end is NULL */
  if(end == NULL) {
    end = ptr-1;
  }

  helper = strndup(begin,end-begin+1);
  if(is_valid_hostname(helper) == -1) {
    /* ups, no valid hostname -- die */
    free(helper);
    return -1;
  }

  free(helper);

  /* hostname is valid; follows a port? */
  if(*ptr == ':') {
    /* port has to be digit+ */
    for(begin=++ptr;*ptr;ptr++) {
      /* port is valid (at least one digit followed by a slash) */
      if(*ptr == '/' && ptr > begin) break;

      /* hu? port must be digits... bad boy! */
      if(!isdigit(*ptr)) return -1;
    }
  }

  /* does not follow host path, no prospero url*/
  if(*ptr++ != '/') return -1;
  
  for(begin=ptr;*ptr;ptr++) {
    /* escape sequenz */
    if(*ptr == '%') {
      if(!isxdigit(*(ptr+1)) || !isxdigit(*(ptr+2))) return -1;
      ptr += 2;
      continue;
    }
    
  
    if(!isuchar_wo_escape(*ptr)) {
      switch(*ptr) {
        case '/':
        case '?':
        case ':':
        case '@':
        case '&':
        case '=':
          continue;
        default:
          if(*ptr != ';') return -1;
      }
      /* could only be a semicolon (end of host path) */
      break;
    }
  }
  
  /* follows a fieldspec? */
  if(*ptr == ';') {
    for(++ptr;*ptr;ptr++) {
      /* escaped character */
      if(*ptr == '%') {
        if(!isxdigit(*(ptr+1)) || !isxdigit(*(ptr+2))) return -1;
        ptr += 2;
        continue;
      }

      /* we checked for escaped before */
      if(!isuchar_wo_escape(*ptr)) {
        switch(*ptr) {
          case ';':
            if(!had_cur_equal) return -1;
            had_cur_equal = 0;
            break;
          case '=':
            if(had_cur_equal) return -1;
            had_cur_equal = 1;
            break;
          case '?':
          case ':':
          case '@':
          case '&':
            break;
          default:
            /* no anchor in strict mode */
            return -1;
        }
      }
    }
  }
  
  /* now url should definitely be at an end! */
  if (*ptr) return -1;
  
  return 0;
}
/* }}} */

/* {{{ is_valid_telnet_link */
int is_valid_telnet_link(const u_char *link) {
  register const u_char *ptr;
  const u_char *anchor,*doubledot;
  u_char *hostname;

  if(cf_strncmp(link,"telnet://",8)) return -1;

  if((anchor = strstr(link,"@")) == NULL) {
    if((doubledot = strstr(link+8,":")) == NULL) {
      hostname = strdup(link+8);
      if(is_valid_hostname(hostname)) {
        free(hostname);
        return -1;
      }
      free(hostname);
    }
    else {
      hostname = strndup(link+8,link+8-doubledot);
      if(is_valid_hostname(hostname)) {
        free(hostname);
        return -1;
      }
      free(hostname);

      for(ptr=doubledot+1;*ptr;ptr++) {
        if(!isdigit(*ptr)) {
          /* we do not allow anything else after the slash */
          if(*ptr == '/' && *(ptr+1)) return -1;
        }
      }
    }
  }
  else {
    if((doubledot = strstr(anchor+1,":")) == NULL) {
      if(is_valid_hostname(anchor+1)) return -1;
    }
    else {
      hostname = strndup(anchor+1,anchor+1-doubledot);
      if(is_valid_hostname(hostname)) {
        free(hostname);
        return -1;
      }
      free(hostname);

      for(ptr=doubledot+1;*ptr;ptr++) {
        if(!isdigit(*ptr)) {
          /* we do not allow anything else after the slash */
          if(*ptr == '/' && *(ptr+1)) return -1;
        }
      }
    }
  }

  /* ok then -- it seems as if hostname and port are all right, check username
   * and password, if exist
   */
  if(anchor) {
    for(ptr=link+8;*ptr;ptr++) {
      if(isuchar_wo_escape(*ptr) == 0) {
        /* end of username */
        if(*ptr == ':' || *ptr == '@') break;

        /* escaped char */
        if(*ptr == '%') {
          if(isxdigit(*(ptr+1)) == 0 || isxdigit(*(ptr+2)) == 0) return -1;
        }

        /* there are some special characters allowed */
        switch(*ptr) {
          case ';':
          case '?':
          case '&':
          case '=':
            break;
          default:
            return -1;
        }
      }

    }

    /* password       = *[ uchar | ";" | "?" | "&" | "=" ] */
    if(*ptr == ':') {
      for(ptr++;*ptr;ptr++) {
        if(isuchar_wo_escape(*ptr) == 0) {
          if(*ptr == '%') {
            if(isxdigit(*(ptr+1)) == 0 || isxdigit(*(ptr+2)) == 0) return -1;
          }

          switch(*ptr) {
            case ';':
            case '?':
            case '&':
            case '=':
              break;
            default:
              return -1;
          }
        }
      }
    }

    /* we have to be at the end of the string now */
    if(*ptr) return -1;
  }


  return 0;
}
/* }}} */

/* {{{ is_valid_nntp_link */
int is_valid_nntp_link(const u_char *link) {
  register const u_char *ptr;
  const u_char *end;
  u_char *hostname;

  if(cf_strncmp(link,"nntp://",7)) return -1;
  else ptr = link + 7;

  if((end = strstr(ptr,"/")) == NULL) return -1;

  hostname = strndup(ptr,end-ptr);
  if(is_valid_hostname(hostname)) {
    free(hostname);
    return -1;
  }
  free(hostname);

  ptr = end + 1;
  if(!isalpha(*ptr)) return -1;

  for(;*ptr;ptr++) {
    if(*ptr == '/') break;

    if(!isalnum(*ptr)) {
      switch(*ptr) {
        case '-':
        case '.':
        case '+':
        case '_':
          break;
        default:
          return -1;
      }
    }
  }

  if(*ptr == '/') {
    for(;*ptr;ptr++) {
      if(!isdigit(*ptr)) return -1;
    }
  }

  return 0;
}
/* }}} */

/* {{{ is_valid_news_link */
int is_valid_news_link(const u_char *link) {
  return 0;
}
/* }}} */

/* {{{ is_valid_gopher_link */
int is_valid_gopher_link(const u_char *link) {
  register const u_char *ptr;
  const u_char *begin = NULL,*end = NULL;
  u_char *helper;

  /* first we check if the scheme is valid */
  if(cf_strncmp(link,"gopher://",9)) return -1;
  
  ptr = (u_char *)link + 9;

  begin = ptr;

  /* ok, it seems as if scheme is valid -- get hostname */
  for(;*ptr;ptr++) {
    if(*ptr == ':' || *ptr == '/') {
      end = ptr-1;
      break;
    }
  }

  /* URL consists only of a hostname if end is NULL */
  if(end == NULL) {
    end = ptr-1;
  }

  helper = strndup(begin,end-begin+1);
  if(is_valid_hostname(helper) == -1) {
    /* ups, no valid hostname -- die */
    free(helper);
    return -1;
  }

  free(helper);

  /* hostname is valid; follows a port? */
  if(*ptr == ':') {
    /* port has to be digit+ */
    for(begin=++ptr;*ptr;ptr++) {
      /* port is valid (at least one digit followed by a slash) */
      if(*ptr == '/' && ptr > begin) break;

      /* hu? port must be digits... bad boy! */
      if(!isdigit(*ptr)) return -1;
    }
  }
  
  if(!*ptr) return 0;
  if(*ptr++ != '/') {
    return -1;
  }
  
  for(begin=ptr;*ptr;ptr++) {
    /* escape sequenz */
    if(*ptr == '%') {
      // follows search?
      if(*(ptr+1) == '0' && *(ptr+2) == '9') {
        ptr += 3;
        break;
      }
      if(!isxdigit(*(ptr+1)) || !isxdigit(*(ptr+2))) return -1;
      ptr += 2;
      continue;
    }
    
    if(!isuchar_wo_escape(*ptr)) {
      switch(*ptr) {
        case ';':
        case '/':
        case '?':
        case ':':
        case '@':
        case '&':
        case '=':
          continue;
        default:
          return -1;
      }
    }
  }
  
  // valid (search may be empty because it is defined as *[ ... ]!)
  if(!*ptr) return 0;

  /* follows a search path */
  for(;*ptr;ptr++) {
    /* escaped character */
    if(*ptr == '%') {
      // follows gopher+?
      if(*(ptr+1) == '0' && *(ptr+2) == '9') {
        ptr += 3;
        break;
      }
      if(!isxdigit(*(ptr+1)) || !isxdigit(*(ptr+2))) return -1;
      ptr += 2;
      continue;
    }

    /* we checked for escaped before */
    if(!isuchar_wo_escape(*ptr)) {
      switch(*ptr) {
        case ';':
        case ':':
        case '@':
        case '&':
        case '=':
          break;
        default:
          /* no anchor in strict mode */
          return -1;
      }
    }
  }
  
  // valid (gopher+ may be empty because it is defined as *xchar!)
  if(!*ptr) return 0;

  for(;*ptr;ptr++) {
    /* escaped character */
    if(*ptr == '%') {
      if(!isxdigit(*(ptr+1)) || !isxdigit(*(ptr+2))) return -1;
      ptr += 2;
      continue;
    }
    /* we checked for escaped before */
    if(!isuchar_wo_escape(*ptr)) {
      switch(*ptr) {
        case ';':
        case '/':
        case '?':
        case ':':
        case '@':
        case '&':
        case '=':
          break;
        default:
          /* no anchor in strict mode */
          return -1;
      }
    }
  }

  return 0;
}
/* }}} */

/* {{{ is_valid_ftp_link */
int is_valid_ftp_link(const u_char *link) {
  register const u_char *ptr = link;
  const u_char *at,*slash,*doublepoint,*start;
  u_char *hostname;
  int dd = 0;

  if(cf_strncmp(link,"ftp://",6)) return -1;

  /*
   * it doesnt matter if we only have a username or a password
   * and a username. Both consist of *[uchar|";"|"?"|"&"|"="]
   */
  if((at = strstr(link+6,"@")) != NULL) {
    for(ptr=link+6;ptr<at;ptr++) {
      if(!isuchar_wo_escape(*ptr)) {
        if(*ptr == '%') {
          if(!isxdigit(*(ptr+1)) || !isxdigit(*(ptr+2))) return -1;
          ptr += 2;
          continue;
        }

        switch(*ptr) {
          case ';':
          case '?':
          case '&':
          case '=':
            break;
          case ':':
            if(dd == 0) {
              dd = 1;
              break;
            }
          default:
            return -1;
        }
      }
    }
  }

  start = at ? at + 1 : link + 6;
  
  slash       = strstr(start,"/");
  doublepoint = strstr(start,":");

  if(!slash) {
    if(!doublepoint) {
      if(!is_valid_hostname(start)) return -1;
    }
    else {
      hostname = strndup(start,doublepoint-start);
      if(!is_valid_hostname(hostname)) {
        free(hostname);
        return -1;
      }
      free(hostname);

      for(ptr=doublepoint+1;*ptr;ptr++) {
        if(!isdigit(*ptr)) return -1;
      }
    }
  }
  /* a slash exists */
  else {
    if(doublepoint) {
      hostname = strndup(start,doublepoint-start);
      if(!is_valid_hostname(hostname)) {
        free(hostname);
        return -1;
      }
      free(hostname);

      for(ptr=doublepoint+1;*ptr != '/';ptr++) {
        if(!isdigit(*ptr)) return -1;
      }
    }
    else {
      hostname = strndup(start,slash-start);
      if(!is_valid_hostname(hostname)) {
        free(hostname);
        return -1;
      }
      free(hostname);
    }
  }

  if(slash) {
    for(ptr=slash+1;*ptr;ptr++) {
      if(!isuchar_wo_escape(*ptr)) {
        if(*ptr == '%') {
          if(!isxdigit(*(ptr+1)) || !isxdigit(*(ptr+2))) return -1;
          ptr += 2;
          continue;
        }

        if(*ptr == ';') {
          break;
        }

        switch(*ptr) {
          case '?':
          case ':':
          case '@':
          case '&':
          case '=':
            break;
          default:
            return -1;
        }
      }
    }
  }

  if(*ptr == ';') {
    switch(*(ptr+1)) {
      case 'A':
      case 'I':
      case 'D':
      case 'a':
      case 'i':
      case 'd':
        break;
      default:
        return -1;
    }

    if(*(ptr+2)) return -1;
  }

  return 0;
}
/* }}} */

/* {{{ is_valid_link */
/**
 * Wrapper function; it checks for a scheme and calls the
 * scheme validation function
 */
int is_valid_link(const u_char *link) {
  u_char *ptr = strstr(link,"://");
  u_char scheme[20];
  int i;

  /*
   * no scheme found, but mailto-links are mailto:<address> and news-links are
   * news:<address>, not mailto://<address> and not news://<address> -- so we
   * have to do an extra check
   */
  if(ptr == NULL) {
    if(cf_strncmp(link,"mailto:",7) == 0) {
      strcpy(scheme,"mailto");
    }
    else if(cf_strncmp(link,"news:",5) == 0) {
      strcpy(scheme,"news");
    }
    else return -1;
  }
  else {
    if(ptr-link >= 20) return -1; /* hu, seems not to be a valid scheme */

    strncpy(scheme,link,ptr-link);
    scheme[ptr-link] = '\0';
  }

  for(i=0;scheme_list[i].validator;i++) {
    if(cf_strcmp(scheme_list[i].scheme,scheme) == 0) {
      return scheme_list[i].validator(link);
    }
  }

  return -1;
}
/* }}} */

/* eof */