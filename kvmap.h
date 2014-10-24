/* A simple string-to-string key-value map. */

#ifndef KVMAP_H
#define KVMAP_H

struct kvmap;

struct kvmap* new_kvmap(void);
void destroy_kvmap(struct kvmap* kvm);

const char* kvmap_get(const struct kvmap* kvm, const char* key);
void kvmap_put(struct kvmap* kvm, const char* key, const char* value);

typedef void (kvm_callback)(const char* key, const char* value, void* arg);
void kvmap_foreach(const struct kvmap* kvm, kvm_callback* fn, void* arg);

#endif /* KVMAP_H */
