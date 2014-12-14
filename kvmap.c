/*
 * An utterly braindead O(n), pointer-chasing key-value map.  Currently only
 * used for setup-time config params, so the (lack of) performance shouldn't
 * matter.  If the need arises the implementation can be replaced with
 * something more efficient.
 */

#include <string.h>

#include "misc.h"
#include "kvmap.h"

/* A singly-linked list of key/value pairs. */
struct kvent {
	char* key;
	char* value;
	struct kvent* next;
};

/* And a container for the above. */
struct kvmap {
	struct kvent* head;
};

/* Allocate and return a new (empty) kvmap. */
struct kvmap* new_kvmap(void)
{
	return xcalloc(sizeof(struct kvmap));
}

/* Destroy a kvmap, freeing all associated memory. */
void destroy_kvmap(struct kvmap* kvm)
{
	struct kvent* e;

	while (kvm->head) {
		e = kvm->head;
		kvm->head = e->next;
		xfree(e->key);
		xfree(e->value);
		xfree(e);
	}
	xfree(kvm);
}

/*
 * Return the value associated with the given key in the kvmap (or NULL if the
 * key isn't present).
 */
const char* kvmap_get(const struct kvmap* kvm, const char* key)
{
	const struct kvent* ent;

	for (ent = kvm->head; ent; ent = ent->next) {
		if (!strcmp(ent->key, key))
			return ent->value;
	}

	return NULL;
}

/*
 * Add the given key/value association to the kvmap, replacing any existing
 * value associated with the key.
 */
void kvmap_put(struct kvmap* kvm, const char* key, const char* value)
{
	struct kvent* ent;
	char* v = xstrdup(value);

	for (ent = kvm->head; ent; ent = ent->next) {
		if (!strcmp(ent->key, key)) {
			xfree(ent->value);
			ent->value = v;
			return;
		}
	}

	ent = xmalloc(sizeof(*ent));
	ent->key = xstrdup(key);
	ent->value = v;
	ent->next = kvm->head;
	kvm->head = ent;
}

/* Call fn(key, value, arg) on each key/value pair in the kvmap. */
void kvmap_foreach(const struct kvmap* kvm, kvm_callback* fn, void* arg)
{
	const struct kvent* ent;

	for (ent = kvm->head; ent; ent = ent->next)
		fn(ent->key, ent->value, arg);
}
