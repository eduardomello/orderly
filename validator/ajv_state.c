#include "ajv_state.h"

ajv_node * ajv_alloc_node( orderly_alloc_funcs * alloc ) {
  ajv_node * n = (ajv_node *)
    OR_MALLOC(alloc, sizeof(ajv_node));
  memset((void *) n, 0, sizeof(ajv_node));
  return n;
}

ajv_node * ajv_alloc_node_recursive( orderly_alloc_funcs * alloc, 
                                     orderly_node *n, ajv_node *parent) {
  orderly_node *cur;
  ajv_node *an = ajv_alloc_node(alloc);

  if (n->sibling) {
    an->sibling = ajv_alloc_node_recursive(alloc,n->sibling,parent);
  }
  if (n->child) {
    an->child = ajv_alloc_node_recursive(alloc,n->child, an);
  }
  
  an->node = n;
  an->parent = parent;

  return an;
}

ajv_node * ajv_free_node ( orderly_alloc_funcs * alloc, ajv_node ** n) {
  if (n && *n) {
    if ((*n)->sibling) ajv_free_node(alloc,&((*n)->sibling));
    if ((*n)->child) ajv_free_node(alloc,&((*n)->child));
    /* XXX: when should we free the orderly_node? it will recurse itself */
    OR_FREE(alloc, *n);
    *n = NULL;
  }
}

ajv_node * ajv_reset_node (ajv_node * n) {
  n->seen = 0;
  if (n->sibling) ajv_reset_node_state(n->sibling);
  if (n->child) ajv_reset_node_state(n->child);
}
  
