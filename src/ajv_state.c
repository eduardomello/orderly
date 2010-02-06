#include <yajl/yajl_parse.h>
#include "ajv_state.h"
#include "yajl_interface.h"
#include "api/ajv_parse.h"
#include "orderly_json.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

ajv_node * ajv_alloc_node( const orderly_alloc_funcs * alloc, 
                           const orderly_node *on,    ajv_node *parent ) 
{
  ajv_node *n = (ajv_node *)OR_MALLOC(alloc, sizeof(ajv_node));
  memset((void *) n, 0, sizeof(ajv_node));
  const char *regerror = NULL;
  int erroffset;
  n->parent = parent;
  n->node   = on;
  if (on->regex) {
    n->regcomp = pcre_compile(on->regex,
                              0,
                              &regerror,
                              &erroffset,
                              NULL);
  }
    
  return n;
}

ajv_node * ajv_alloc_tree(const orderly_alloc_funcs * alloc,
                          const orderly_node *n, ajv_node *parent) {

  ajv_node *an = ajv_alloc_node(alloc, n, parent);

  if (n->sibling) an->sibling = ajv_alloc_tree(alloc,n->sibling,parent);
  if (n->child)   an->child   = ajv_alloc_tree(alloc,n->child, an);
  
  return an;
}

void ajv_free_node (const orderly_alloc_funcs * alloc, ajv_node ** n) {
  if (n && *n) {
    if ((*n)->sibling) ajv_free_node(alloc,&((*n)->sibling));
    if ((*n)->child) ajv_free_node(alloc,&((*n)->child));
    /* the orderly_node *  belongs to the schema, don't free it */
    if ((*n)->regcomp) {
      pcre_free((*n)->regcomp);
    }
    OR_FREE(alloc, *n);
    *n = NULL;
  }
}

void ajv_reset_node (ajv_node * n) {
  n->required = 0;
  n->seen     = 0;

  if (n->sibling) ajv_reset_node(n->sibling);
  if (n->child) ajv_reset_node(n->child);
}
  
void ajv_clear_error (ajv_state s) {

  if (s->error.extra_info) OR_FREE(s->AF,s->error.extra_info);

  s->error.code       = ajv_e_no_error;
  s->error.extra_info = NULL;
  s->error.node       = NULL;


}

void ajv_set_error ( ajv_state s, ajv_error e,
                     const ajv_node * node, const char *info ) {

  ajv_clear_error(s);
  s->error.node = node;
  s->error.code = e;
  if (info) {
    BUF_STRDUP(s->error.extra_info, 
               s->AF, info, strlen(info));
  }

}

const char * ajv_error_to_string (ajv_error e) {

  const char *outbuf;
  switch(e) {
  case ajv_e_type_mismatch:  
    outbuf = "type mismatch"; break;
  case ajv_e_trailing_input: 
    outbuf = "input continued validation completed"; break;
  case ajv_e_out_of_range:   
    outbuf = "value out of range"; break;
  case ajv_e_incomplete_container: 
    outbuf = "incomplete structure"; break;
  case ajv_e_illegal_value:  
    outbuf = "value not permitted"; break;
  case ajv_e_regex_failed:  
    outbuf = "string did not match regular expression"; break;
  case ajv_e_unexpected_key: 
    outbuf = "key not permitted"; break;
  default:                   
    outbuf = "Internal error: unrecognized error code"; 
  };
  
  return outbuf;
}

#define ERROR_BASE_LENGTH 1024
unsigned char * ajv_get_error(ajv_handle hand, int verbose,
                              const unsigned char * jsonText,
                              unsigned int jsonTextLength) {
  char * yajl_err;
  char * ret;
  ajv_state s = hand;

  struct ajv_error_t *e = &(s->error);

  int yajl_length;
  int max_length;

  if (e->code == ajv_e_no_error) { 
    return yajl_get_error(hand->yajl,verbose,jsonText,jsonTextLength);
  } 

  /* include the yajl error message when verbose */
  if (verbose == 1) {
    yajl_err = 
      (char *)yajl_get_error(hand->yajl,verbose,
                             (unsigned char *)jsonText,jsonTextLength);

    yajl_length = strlen(yajl_err);
  }

  max_length  = ERROR_BASE_LENGTH;
  max_length += e->extra_info ? strlen(e->extra_info) : 1;
  max_length += yajl_length;
  if (e->node && e->node->node->name) {
    max_length += strlen(e->node->node->name);
  }
  ret = OR_MALLOC(s->AF, max_length+1);
  ret[0] = '\0';
  strcat(ret, "VALIDATION ERROR:");
  if (e->node && e->node->node->name) {
    strcat(ret," value for map key '");
    strcat(ret,e->node->node->name); /* XXX */
    strcat(ret,"':");
  }
  strcat(ret, (const char *)ajv_error_to_string(e->code));
  /** TODO: produce more details, info is available */
  strcat(ret,"\n");
  if (yajl_err) {
    strcat(ret,yajl_err);
    yajl_free_error(hand->yajl, (unsigned char *)yajl_err);
  }
  return (unsigned char *)ret;
}

void ajv_free_error(ajv_handle hand, unsigned char *str) {
  OR_FREE(hand->AF, str);
}


yajl_status ajv_parse_and_validate(ajv_handle hand,
                                   const unsigned char * jsonText,
                                   unsigned int jsonTextLength,
                                   ajv_schema schema) {
  yajl_status stat;

  if (schema) {
    ajv_clear_error(hand);
    hand->s = schema;
    hand->node = schema->root;
    ajv_reset_node(hand->node);
  } 

  stat = yajl_parse(hand->yajl, jsonText, jsonTextLength);
  if (hand->error.code != ajv_e_no_error) {
    assert(stat == yajl_status_client_canceled);
    stat = yajl_status_error;
  } 

  return stat;
}

yajl_status ajv_validate(ajv_handle hand,
                        ajv_schema schema,
                        orderly_json *json) {
  yajl_status ret =  yajl_status_ok;
  int cancelled;
  ajv_clear_error(hand);
  hand->s = schema;
  hand->node = schema->root;

  ajv_reset_node(hand->node);

  cancelled = orderly_synthesize_callbacks(&ajv_callbacks,hand,json);
  if (cancelled == 1) {
    if (hand->error.code == ajv_e_no_error) {
      ret = yajl_status_client_canceled;
    } else {
      ret = yajl_status_error;
    }
  }
  
  return ret;
}

ajv_handle ajv_alloc(const yajl_callbacks * callbacks,
                     const yajl_parser_config * config,
                     const yajl_alloc_funcs * allocFuncs,
                     void * ctx) {
  const orderly_alloc_funcs * AF = (const orderly_alloc_funcs *) allocFuncs;
  {
    static orderly_alloc_funcs orderlyAllocFuncBuffer;
    static orderly_alloc_funcs * orderlyAllocFuncBufferPtr = NULL;
    
    if (orderlyAllocFuncBufferPtr == NULL) {
            orderly_set_default_alloc_funcs(&orderlyAllocFuncBuffer);
            orderlyAllocFuncBufferPtr = &orderlyAllocFuncBuffer;
    }
    AF = orderlyAllocFuncBufferPtr;

    }

  
  struct ajv_state_t *ajv_state = 
    (struct ajv_state_t *)
    OR_MALLOC(AF, sizeof(struct ajv_state_t));
  memset((void *) ajv_state, 0, sizeof(struct ajv_state_t));
  ajv_state->AF = AF;
  ajv_state->any.parent = ajv_state->any.child = ajv_state->any.sibling = NULL;
  ajv_state->any.node = orderly_alloc_node((orderly_alloc_funcs *)AF, 
                                           orderly_node_any);
  ajv_state->cb = callbacks;
  ajv_state->cbctx = ctx;

  ajv_state->yajl = yajl_alloc(&ajv_callbacks,
                               config,
                               allocFuncs,
                               (void *)ajv_state);
  return ajv_state;
}


void ajv_free(ajv_handle hand) {
  const orderly_alloc_funcs *AF = hand->AF;

  ajv_clear_error(hand);

  orderly_free_node(hand->AF,(orderly_node **)&(hand->any.node));

  yajl_free(hand->yajl);
  OR_FREE(AF,hand);

}

yajl_status ajv_parse_complete(ajv_handle hand) {
  yajl_status stat = yajl_parse_complete(hand->yajl);

  if (stat == yajl_status_ok || stat == yajl_status_insufficient_data) {
    if (! hand->s->root->seen ) {
      ajv_set_error(hand, ajv_e_incomplete_container, NULL, "Empty root");
      stat = yajl_status_error;
    }   
  }


  return stat;
}
