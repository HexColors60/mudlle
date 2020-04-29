#ifndef ASSOC_H
#define ASSOC_H

#include <stdbool.h>
#include <stddef.h>

struct assoc_array_type {
  int (*cmp)(const void *, const void *);
  unsigned (*hash)(const void *);
  void (*free_key)(void *);
  void (*free_data)(void *);
};

struct assoc_array {
  const struct assoc_array_type *type;
  int used, size_bits;          /* size_bits == 0 <=> nodes == NULL */
  struct assoc_array_node **nodes;
};

void assoc_array_set(struct assoc_array *assoc, const void *key, void *data);
void *assoc_array_lookup(const struct assoc_array *assoc, const void *key);
void **assoc_array_lookup_ref(const struct assoc_array *assoc,
                              const void *key);
int assoc_array_remove(struct assoc_array *assoc, const void *key);

bool assoc_array_exists(struct assoc_array *assoc,
			bool (*f)(const void *key, void *data, void *idata),
                        void *idata);
void assoc_array_free(struct assoc_array *assoc);

/* data is malloced and will be freed automatically */
extern const struct assoc_array_type long_to_mallocp_assoc_array_type;
/* data will not be freed automatically from these */
extern const struct assoc_array_type long_to_voidp_assoc_array_type;
extern const struct assoc_array_type const_charp_to_voidp_assoc_array_type;
extern const struct assoc_array_type const_icharp_to_voidp_assoc_array_type;

#endif
