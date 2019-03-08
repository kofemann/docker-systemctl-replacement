/**
 * A return-type 'restrict' points to a newly allocated object.
 * A method ending in 's' will store and free the given object.
 * A method without an 's' duplicates the value and the caller
 * is responsible to potentially free the argument object. If
 * argument was a static object (e.g. a string literal) that's
 * okay as well and the target buffer contains a copy.
 *
 * str_t buffer;                      // that's a <char*> var
 * str_set(&buffer, "foo");           // implicitly dup's it
 * str_sets(&buffer, str_dup("foo")); // so same as this.
 * str_sets(&buffer, str_dup2("foo", "bar")); // joined parts
 * str_null(&buffer);              // free buffer and set null
 *
 * In these examples 'str_sets' checks if &buffer already has
 * a string pointer that needs to be free()d before the new 
 * value will be written to it. The deinit '_null' part can do
 * a lot of things but for str_t it sets str_NULL instead of 
 * the init'ed str_EMPTY so it is almost the reverse here.
 */

#include <regex.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include "systemctl-logging.h"

static int
regmatch(const char* regex, const char* text, size_t nmatch, regmatch_t pmatch[], char* flags) 
{
  int res; /* 0 = success */
  int cflags = REG_EXTENDED;
  if (flags && strchr(flags, 'i'))
      cflags |= REG_ICASE;
  if (flags && strchr(flags, 'm'))
      cflags |= REG_NEWLINE;
  regex_t preg;
  res = regcomp(&preg, regex, cflags);
  if (res) logg_info("bad regex '%s'", regex);
  res = regexec(&preg, text, nmatch, pmatch, 0);
  regfree(&preg);
  return res;
}

typedef char* str_t;

static inline ssize_t 
str_len(str_t str1)
{
  if (str1 == NULL) return 0;
  return strlen(str1);
}

static inline str_t restrict
str_dup(const str_t str1)
{
  if (str1 == NULL) return NULL;
  return strdup(str1);
}

static inline void
str_cpy(str_t into, const str_t str1)
{
  if (into == NULL) return;
  if (str1 == NULL) return;
  strcpy(into, str1);
}

static inline int
str_cmp(const str_t str1, const str_t str2)
{
  if (str1 == NULL || str2 == NULL) {
      if (str1 && ! str2) {
          return -1;
      }
      if (! str1 && str2) {
          return 1;
      }
      return 0;
  }
  return strcmp(str1, str2);
}


/* types */

typedef void* ptr_list_entry_t;
typedef str_t str_list_entry_t;

typedef struct ptr_list 
{
  ssize_t size;
  ptr_list_entry_t* data;
} ptr_list_t;

typedef struct str_list 
{
  ssize_t size;
  str_list_entry_t* data;
} str_list_t;

typedef struct str_list_list
{
  ssize_t size;
  str_list_t* data;
} str_list_list_t;

typedef struct str_dict_entry 
{
  str_t key;
  str_t value;
} str_dict_entry_t;

typedef struct str_dict 
{
  ssize_t size;
  str_dict_entry_t* data;
} str_dict_t;

typedef struct str_list_dict_entry 
{
  str_t key;
  str_list_t value;
} str_list_dict_entry_t;

typedef struct str_list_dict
{
  ssize_t size;
  str_list_dict_entry_t* data;
} str_list_dict_t;

typedef struct str_list_dict_dict_entry 
{
  str_t key;
  str_list_dict_t value;
} str_list_dict_dict_entry_t;

typedef struct str_list_dict_dict
{
  ssize_t size;
  str_list_dict_dict_entry_t* data;
} str_list_dict_dict_t;

typedef struct ptr_dict_entry 
{
  str_t key;
  void* value;
} ptr_dict_entry_t;

typedef struct ptr_list_dict_entry 
{
  str_t key;
  ptr_list_t value;
} ptr_list_dict_entry_t;

typedef void (*free_func_t)(void*);

typedef struct ptr_dict
{
  ssize_t size;
  ptr_dict_entry_t* data;
  free_func_t free;
} ptr_dict_t;

typedef struct ptr_list_dict
{
  ssize_t size;
  ptr_list_dict_entry_t* data;
  free_func_t free;
} ptr_list_dict_t;

/* init */

#define str_NULL NULL
#define str_EMPTY { '\0' }

static void
str_init(str_t* self)
{
   *self = malloc(1);
   **self = '\0';
}

static void
str_init0(str_t* self, ssize_t size)
{
   *self = malloc(size+1);
   **self = '\0';
}

static void 
str_init_from(str_t* self, char* str)
{
  *self = malloc(str_len(str));
  str_cpy(*self, str);
}

#define str_list_NULL { 0, NULL }
#define str_list_list_NULL { 0, NULL }
#define str_dict_NULL { 0, NULL }
#define str_dict_NULL { 0, NULL }
#define str_list_dict_NULL { 0, NULL }
#define str_list_dict_dict_NULL { 0, NULL }

static void 
str_list_init0(str_list_t* self, ssize_t size)
{
  self->size = size;
  self->data = calloc(size, sizeof(str_list_entry_t));
}

static void
str_list_init(str_list_t* self)
{
  self->size = 0;
  self->data = NULL;
}

static void
str_list_init_from(str_list_t* self, int size, char** data) 
{
  str_list_init0(self, size);
  for (int i=0; i < size; ++i) {
    self->data[i] = str_dup(data[i]);
  }
}

static void 
str_list_list_init0(str_list_list_t* self, ssize_t size)
{
  self->size = size;
  self->data = calloc(size, sizeof(str_list_t));
}

static void
str_list_list_init(str_list_list_t* self)
{
  self->size = 0;
  self->data = NULL;
}

static void
str_list_list_init_from(str_list_list_t* self, int size, char** data) 
{
  str_list_list_init0(self, size);
  for (int i=0; i < size; ++i) {
    str_list_init0(&self->data[i], 1);
    self->data[i].data[0] = str_dup(data[i]);
  }
}

static void 
str_dict_init0(str_dict_t* self, ssize_t size)
{
  self->size = size;
  self->data = calloc(size, sizeof(str_dict_entry_t));
}

static void
str_dict_init(str_dict_t* self)
{
  self->size = 0;
  self->data = NULL;
}

static void 
str_list_dict_init0(str_list_dict_t* self, ssize_t size)
{
  self->size = size;
  self->data = calloc(size, sizeof(str_list_dict_entry_t));
}

static void
str_list_dict_init(str_list_dict_t* self)
{
  self->size = 0;
  self->data = NULL;
}

static void 
str_list_dict_dict_init0(str_list_dict_dict_t* self, ssize_t size)
{
  self->size = size;
  self->data = calloc(size, sizeof(str_list_dict_dict_entry_t));
}

static void
str_list_dict_dict_init(str_list_dict_dict_t* self)
{
  self->size = 0;
  self->data = NULL;
}

static void 
ptr_dict_init0(ptr_dict_t* self, ssize_t size, free_func_t free)
{
  self->size = size;
  self->data = calloc(size, sizeof(ptr_dict_entry_t));
  self->free = free;
}

static void
ptr_dict_init(ptr_dict_t* self, free_func_t free)
{
  self->size = 0;
  self->data = NULL;
  self->free = free;
}

/* del */

static void 
str_null(str_t* self)
{
    if (self) {
        if (*self) {
            free(*self);
        }
        *self = NULL;
    }
}

static void 
str_list_null(str_list_t* self)
{
  if (self->data) {
    for (ssize_t i=0; i < self->size; ++i) {
      if (self->data[i]) {
        free(self->data[i]);
        self->data[i] = NULL;
      }
    }
    free(self->data);
  }
  self->size = 0;
  self->data = NULL;
}

static void 
str_list_list_null(str_list_list_t* self)
{
  if (self->data) {
    for (ssize_t i=0; i < self->size; ++i)
    {
      str_list_null(& self->data[i]);
    }
    free(self->data);
  }
  self->size = 0;
  self->data = NULL;
}

static void 
str_dict_null(str_dict_t* self)
{
  if (self->data) {
    for (ssize_t i=0; i < self->size; ++i)
    {
      str_null(& self->data[i].key);
      str_null(& self->data[i].value);
    }
    free(self->data);
  }
  self->size = 0;
  self->data = NULL;
}

static void 
str_list_dict_null(str_list_dict_t* self)
{
  if (self->data) {
    for (ssize_t i=0; i < self->size; ++i)
    {
      str_null(& self->data[i].key);
      str_list_null(& self->data[i].value);
    }
    free(self->data);
  }
  self->size = 0;
  self->data = NULL;
}

static void 
str_list_dict_dict_null(str_list_dict_dict_t* self)
{
  if (self->data) {
      for (ssize_t i=0; i < self->size; ++i)
      {
          str_null(& self->data[i].key);
          str_list_dict_null(& self->data[i].value);
      }
      free(self->data);
  }
  self->size = 0;
  self->data = NULL;
}

static void
ptr_dict_null(ptr_dict_t* self) 
{
  if (self->data) {
    for(int i = 0; i < self->size; ++i) {
       if (self->data[i].value) {
         self->free(self->data[i].value);
         self->data[i].value = NULL;
       }
       str_null(&self->data[i].key);
    }
    free(self->data);
  }
  self->data = NULL;
  self->size = 0;
}

/* free */

static inline void
str_free(str_t self)
{
    if (self)
    {
       free (self);
    }
}

static inline void
str_list_free(str_list_t* self)
{
  if (self) {
    str_list_null(self);
    free(self);
  }
}

static inline void
str_list_list_free(str_list_list_t* self)
{
  if (self) {
    str_list_list_null(self);
    free(self);
  }
}

static inline void
str_dict_free(str_dict_t* self)
{
  if (self) {
    str_dict_null(self);
    free(self);
  }
}

static inline void
str_list_dict_free(str_list_dict_t* self)
{
  if (self) {
    str_list_dict_null(self);
    free(self);
  }
}

static inline void
str_list_dict_dict_free(str_list_dict_dict_t* self)
{
  if (self) {
    str_list_dict_dict_null(self);
    free(self);
  }
}

static inline void
ptr_dict_free(ptr_dict_t* self)
{
  if (self) {
    ptr_dict_null(self);
    free(self);
  }
}

/* new */

static str_t restrict
str_new()
{
  str_t self;
  str_init(&self);
  return self;
}

static str_list_t* restrict
str_list_new()
{
  str_list_t* self = malloc(sizeof(str_list_t));
  str_list_init(self);
  return self;
}

static str_list_t* restrict
str_list_new0(ssize_t size)
{
  str_list_t* self = malloc(sizeof(str_list_t));
  str_list_init0(self, size);
  return self;
}

static str_list_list_t* restrict
str_list_list_new()
{
  str_list_list_t* self = malloc(sizeof(str_list_list_t));
  str_list_list_init(self);
  return self;
}

static str_dict_t* restrict
str_dict_new()
{
  str_dict_t* self = malloc(sizeof(str_dict_t));
  str_dict_init(self);
  return self;
}

static str_list_dict_t* restrict
str_list_dict_new()
{
  str_list_dict_t* self = malloc(sizeof(str_list_dict_t));
  str_list_dict_init(self);
  return self;
}

static str_list_dict_dict_t* restrict
str_list_dict_dict_new()
{
  str_list_dict_dict_t* self = malloc(sizeof(str_list_dict_dict_t));
  str_list_dict_dict_init(self);
  return self;
}

/* info */

void
logg_info_ptr_dict(str_t msg, const ptr_dict_t* self)
{
  logg_info("ptr_dict=%p (%s)", self->data, msg);
  for (ssize_t i=0; i < self->size; ++i) {
      logg_info("ptr_dict[%i]=%p", i, self->data[i].key);
      logg_info("ptr_dict[%i]='%s'", i, self->data[i].key);
      if (i == 8) {
         logg_info("ptr_dict[.]...");
         break;
      }
  }
}

void
logg_info_ptr_list_dict(str_t msg, const ptr_list_dict_t* self)
{
  logg_info("ptr_list_dict=%p (%s)", self->data, msg);
  for (ssize_t i=0; i < self->size; ++i) {
      logg_info("ptr_list_dict[%i]=%p", i, self->data[i].key);
      logg_info("ptr_list_dict[%i]='%s'", i, self->data[i].key);
      if (i == 8) {
         logg_info("ptr_list_dict[.]...");
         break;
      }
  }
}

void
logg_info_str_dict(str_t msg, const str_dict_t* self)
{
  logg_info("str_dict=%p (%s)", self->data, msg);
  for (ssize_t i=0; i < self->size; ++i) {
      logg_info("str_dict[%i]=%p", i, self->data[i].key);
      logg_info("str_dict[%i]='%s'", i, self->data[i].key);
      if (i == 8) {
         logg_info("str_dict[.]...");
         break;
      }
  }
}

void
logg_info_str_list_dict(str_t msg, const str_list_dict_t* self)
{
  logg_info("str_list_dict=%p (%s)", self->data, msg);
  for (ssize_t i=0; i < self->size; ++i) {
      logg_info("str_list_dict[%i]=%p", i, self->data[i].key);
      logg_info("str_list_dict[%i]='%s'", i, self->data[i].key);
      if (i == 8) {
         logg_info("str_list_dict[.]...");
         break;
      }
  }
}

void
logg_info_str_list_dict_dict(str_t msg, const str_list_dict_dict_t* self)
{
  logg_info("str_list_dict_dict=%p (%s)", self->data, msg);
  for (ssize_t i=0; i < self->size; ++i) {
      logg_info("str_list_dict_dict[%i]=%p", i, self->data[i].key);
      logg_info("str_list_dict_dict[%i]='%s'", i, self->data[i].key);
      if (i == 8) {
         logg_info("str_list_dict_dict[.]...");
         break;
      }
  }
}


/* len */

static inline ssize_t
str_list_len(const str_list_t* self)
{
   return self->size;
}

static inline ssize_t
str_list_list_len(const str_list_list_t* self)
{
   return self->size;
}

static inline ssize_t
str_list_dict_len(const str_list_dict_t* self)
{
   return self->size;
}

static inline ssize_t
str_list_dict_dict_len(const str_list_dict_dict_t* self)
{
   return self->size;
}

/* get ref */

static str_t
str_list_get(const str_list_t* self, const str_t key)
{
  for (ssize_t i=0; i < self->size; ++i) {
    if (! str_cmp(self->data[i], key)) {
       return self->data[i];
    }
  }
  return NULL;
}

static str_t
str_dict_get(const str_dict_t* self, const str_t key)
{
  for (ssize_t i=0; i < self->size; ++i) {
    if (! str_cmp(self->data[i].key, key)) {
       return self->data[i].value;
    }
  }
  return NULL;
}

static str_list_t*
str_list_dict_get(const str_list_dict_t* self, const str_t key)
{
  for (ssize_t i=0; i < self->size; ++i) {
    if (! str_cmp(self->data[i].key, key)) {
       return & self->data[i].value;
    }
  }
  return NULL;
}

static str_list_dict_t*
str_list_dict_dict_get(const str_list_dict_dict_t* self, const str_t key)
{
  for (ssize_t i=0; i < self->size; ++i) {
    if (! str_cmp(self->data[i].key, key)) {
       return & self->data[i].value;
    }
  }
  return NULL;
}

static void*
ptr_dict_get(const ptr_dict_t* self, const str_t key)
{
  for (ssize_t i=0; i < self->size; ++i) {
    if (! str_cmp(self->data[i].key, key)) {
       return self->data[i].value;
    }
  }
  return NULL;
}

/* find */

static ssize_t
str_find(const str_t self, const str_t key)
{
  if (self == NULL) return -1;
  if (key == NULL) return -1;
  str_t found = strstr(self, key);
  if (found == NULL) return -1;
  return found - self;
}

static ssize_t
str_list_find(const str_list_t* self, const str_t key)
{
  for (ssize_t i=0; i < self->size; ++i) {
    if (! str_cmp(self->data[i], key)) {
       return i;
    }
  }
  return -1;
}

/* return the index with an equal or -1 if the key was not found. */
static ssize_t
ptr_dict_find(const ptr_dict_t* self, const str_t key)
{
  if (self->data == NULL)
     return -1;
  if (self->size == 0)
     return -1;
  ///logg_info("'%s' dict_find size=%i", key, self->size);
  ///logg_info_ptr_dict("ptr_dict_find", self);
  /* binary search */
  ssize_t a=0;
  ssize_t e=self->size;
  ssize_t c=e/2;
  while(true) {
    int comp = str_cmp(key, self->data[c].key);
    if (comp == 0) {
       ///logg_info("'%s' equal to '%s' at [%i] -> return %i", key, self->data[c].key, c, c);
       return c;
    } else if (comp < 0) {
       ///logg_info("'%s' lower than '%s' at [%i]", key, self->data[c].key, c);
       ssize_t b = a+(c-a)/2;
       if (b == c) b -= 1;
       if (b < a) { 
           ///logg_info("'%s' lower than '%s' at [%i] -> return -1", key, self->data[c].key, c);
           return -1;
       }
       e = c;
       c = b;
    } else if (comp > 0) {
       ///logg_info("'%s' bigger than '%s' at [%i]", key, self->data[c].key, c);
       ssize_t d = c+(e-c)/2;
       if (d == c) d += 1;
       if (d >= e) {
           ///logg_info("'%s' bigger than '%s' at [%i] -> return -1", key, self->data[c].key, c);
           return -1;
       }
       a = c;
       c = d;
    }
  }
}

static ssize_t
ptr_list_dict_find(const ptr_list_dict_t* self, const str_t key)
{
  if (self->data == NULL)
     return -1;
  if (self->size == 0)
     return -1;
  ///logg_info("'%s' dict_find size=%i", key, self->size);
  ///logg_info_ptr_list_dict("ptr_list_dict_find", self);
  /* binary search */
  ssize_t a=0;
  ssize_t e=self->size;
  ssize_t c=e/2;
  while(true) {
    int comp = str_cmp(key, self->data[c].key);
    if (comp == 0) {
       ///logg_info("'%s' equal to '%s' at [%i] -> return %i", key, self->data[c].key, c, c);
       return c;
    } else if (comp < 0) {
       ///logg_info("'%s' lower than '%s' at [%i]", key, self->data[c].key, c);
       ssize_t b = a+(c-a)/2;
       if (b == c) b -= 1;
       if (b < a) { 
           ///logg_info("'%s' lower than '%s' at [%i] -> return -1", key, self->data[c].key, c);
           return -1;
       }
       e = c;
       c = b;
    } else if (comp > 0) {
       ///logg_info("'%s' bigger than '%s' at [%i]", key, self->data[c].key, c);
       ssize_t d = c+(e-c)/2;
       if (d == c) d += 1;
       if (d >= e) {
           ///logg_info("'%s' bigger than '%s' at [%i] -> return -1", key, self->data[c].key, c);
           return -1;
       }
       a = c;
       c = d;
    }
  }
}

/* find position where the key is equal or the just bigger one.
   If there is no bigger entry then return the end (size). 
   This is used internally to find the insert position. */ 
static ssize_t
ptr_dict_find_pos(const ptr_dict_t* self, const str_t key)
{
  if (self->data == NULL)
     return 0;
  if (self->size == 0)
     return 0;
  ///logg_info("'%s' dict_find_pos size=%i", key, self->size);
  ///logg_info_ptr_dict("ptr_dict_find_pos", self);
  /* binary search */
  ssize_t a=0;
  ssize_t e=self->size;
  ssize_t c=e/2;
  while(true) {
    ///logg_info("a=%i c=%i e=%i", a, c, e);
    ///logg_info("key[c]=%p", self->data[c].key);
    ///logg_info("key[c]='%s'", self->data[c].key);
    int comp = str_cmp(key, self->data[c].key);
    if (comp == 0) {
       ///logg_info("'%s' equal to '%s' at [%i] -> return %i", key, self->data[c].key, c, c);
       return c;
    } else if (comp < 0) {
       ///logg_info("'%s' lower than '%s' at [%i]", key, self->data[c].key, c);
       ssize_t b = a+(c-a)/2;
       if (b == c) b -= 1;
       if (b < a) { 
           ///logg_info("'%s' lower than '%s' at [%i] -> return %i", key, self->data[c].key, c, c);
           return c;
       }
       e = c;
       c = b;
    } else if (comp > 0) {
       ///logg_info("'%s' bigger than '%s' at [%i]", key, self->data[c].key, c);
       ssize_t d = c+(e-c)/2;
       if (d == c) d += 1;
       if (d >= e) {
           ///logg_info("'%s' lower than '%s' at [%i] -> return %i", key, self->data[c].key, c, c+1);
           return c+1;
       }
       a = c;
       c = d;
    }
  }
}

static ssize_t
ptr_list_dict_find_pos(const ptr_list_dict_t* self, const str_t key)
{
  if (self->data == NULL)
     return 0;
  if (self->size == 0)
     return 0;
  ///logg_info("'%s' dict_find_pos size=%i", key, self->size);
  ///logg_info_ptr_list_dict("ptr_list_dict_find_pos", self);
  /* binary search */
  ssize_t a=0;
  ssize_t e=self->size;
  ssize_t c=e/2;
  while(true) {
    ///logg_info("a=%i c=%i e=%i", a, c, e);
    ///logg_info("key[c]=%p", self->data[c].key);
    ///logg_info("key[c]='%s'", self->data[c].key);
    int comp = str_cmp(key, self->data[c].key);
    if (comp == 0) {
       ///logg_info("'%s' equal to '%s' at [%i] -> return %i", key, self->data[c].key, c, c);
       return c;
    } else if (comp < 0) {
       ///logg_info("'%s' lower than '%s' at [%i]", key, self->data[c].key, c);
       ssize_t b = a+(c-a)/2;
       if (b == c) b -= 1;
       if (b < a) { 
           ///logg_info("'%s' lower than '%s' at [%i] -> return %i", key, self->data[c].key, c, c);
           return c;
       }
       e = c;
       c = b;
    } else if (comp > 0) {
       ///logg_info("'%s' bigger than '%s' at [%i]", key, self->data[c].key, c);
       ssize_t d = c+(e-c)/2;
       if (d == c) d += 1;
       if (d >= e) {
           ///logg_info("'%s' lower than '%s' at [%i] -> return %i", key, self->data[c].key, c, c+1);
           return c+1;
       }
       a = c;
       c = d;
    }
  }
}



static ssize_t
str_dict_find(const str_dict_t* self, const str_t key)
{
    const ptr_dict_t* dict = (const ptr_dict_t*)(self);
    return ptr_dict_find(dict, key);
}

static ssize_t
str_dict_find_pos(const str_dict_t* self, const str_t key)
{
    ///logg_info_str_dict("str_dict_find_pos", self);
    const ptr_dict_t* dict = (const ptr_dict_t*)(self);
    return ptr_dict_find_pos(dict, key);
}

static ssize_t
str_list_dict_find(const str_list_dict_t* self, const str_t key)
{
    const ptr_list_dict_t* dict = (const ptr_list_dict_t*)(self);
    return ptr_list_dict_find(dict, key);
}

static ssize_t
str_list_dict_dict_find(const str_list_dict_dict_t* self, const str_t key)
{
    const ptr_list_dict_t* dict = (const ptr_list_dict_t*)(self);
    return ptr_list_dict_find(dict, key);
}


static ssize_t
str_list_dict_find_pos(const str_list_dict_t* self, const str_t key)
{
    ///logg_info_str_list_dict("str_list_dict_find_pos", self);
    const ptr_list_dict_t* dict = (const ptr_list_dict_t*)(self);
    return ptr_list_dict_find_pos(dict, key);
}

static ssize_t
str_list_dict_dict_find_pos(const str_list_dict_dict_t* self, const str_t key)
{
    ///logg_info_str_list_dict_dict("str_list_dict_dict_find_pos", self);
    const ptr_list_dict_t* dict = (const ptr_list_dict_t*)(self);
    return ptr_list_dict_find_pos(dict, key);
}

/* contains */

static inline bool
str_contains(const str_t self, const str_t key)
{
   return strstr(self, key) != NULL;
}

static bool 
str_list_contains(const str_list_t* self, const str_t key)
{
  return str_list_find(self, key) >= 0;
}

static bool 
str_dict_contains(const str_dict_t* self, const str_t key)
{
  return str_dict_find(self, key) >= 0;
}

static bool 
str_list_dict_contains(const str_list_dict_t* self, const str_t key)
{
  return str_list_dict_find(self, key) >= 0;
}

static bool 
str_list_dict_dict_contains(const str_list_dict_dict_t* self, const str_t key)
{
  return str_list_dict_dict_find(self, key) >= 0;
}

static bool 
str_list3_contains(const str_t str1, const str_t str2, const str_t str3, const str_t key)
{
  if (str_cmp(str1, key)) return true;
  if (str_cmp(str2, key)) return true;
  if (str_cmp(str3, key)) return true;
  return false;
}

static bool 
ptr_dict_contains(const ptr_dict_t* self, const str_t key)
{
  return ptr_dict_find(self, key) >= 0;
}

/* equal */
static inline bool
str_equal(const str_t str1, const str_t str2)
{
    return ! str_cmp(str1, str2);
}

static bool
str_list_equal(const str_list_t* list1, const str_list_t* list2)
{
    if (list1->size != list2->size) {
        return false;
    }
    for (int i=0; i < list1->size; ++i) {
        if (str_cmp(list1->data[i], list2->data[i])) {
            return false;
        }
    }
    return true;
}

static bool
str_list_list_equal(const str_list_list_t* list1, const str_list_list_t* list2)
{
    if (list1->size != list2->size) {
        return false;
    }
    for (int i=0; i < list1->size; ++i) {
        if (! str_list_equal(&list1->data[i], &list2->data[i])) {
            return false;
        }
    }
    return true;
}

/* keys */

static str_list_t* restrict
str_dict_keys(const str_dict_t* self)
{
  str_list_t* res = malloc(sizeof(str_list_t));
  res->size = self->size;
  res->data = malloc(res->size * sizeof(str_t));
  for (ssize_t i=0; i < res->size; ++i) {
    res->data[i] = str_dup(self->data[i].key);
  }
  return res;
}

static str_list_t* restrict
str_list_dict_keys(const str_list_dict_t* self)
{
  str_list_t* res = malloc(sizeof(str_list_t));
  res->size = self->size;
  res->data = malloc(res->size * sizeof(str_t));
  for (ssize_t i=0; i < res->size; ++i) {
    res->data[i] = str_dup(self->data[i].key);
  }
  return res;
}

static str_list_t* restrict
str_list_dict_dict_keys(const str_list_dict_dict_t* self)
{
  str_list_t* res = malloc(sizeof(str_list_t));
  res->size = self->size;
  res->data = malloc(res->size * sizeof(str_t));
  for (ssize_t i=0; i < res->size; ++i) {
    res->data[i] = str_dup(self->data[i].key);
  }
  return res;
}


/* copy */

static bool
str_copy(str_t* self, const str_t* from)
{
  if (from == NULL) return false;
  if (*self) free(*self);
  ssize_t size = str_len(*from);
  *self = malloc(size+1);
  str_cpy(*self, *from);
  return true;
}

static bool
str_copy_str_list(str_t* self, const str_list_t* from) 
{
    if (self == NULL) return false;
    ssize_t size = 0;
    for (int i=0; i < from->size; ++i) {
        size += str_len(from->data[i]);
    }
    *self = malloc(size+1);
    ssize_t p = 0;
    for (int i=0; i < from->size; ++i) {
        str_cpy(*self + p, from->data[i]);
        p += str_len(from->data[i]);
    }
    return true;
}

static bool
str_list_copy(str_list_t* self, const str_list_t* from) 
{
   if (self == NULL) return false;
  str_list_null(self);
  self->size = from->size;
  self->data = malloc(from->size * sizeof(str_t));
  for (ssize_t i=0; i < from->size; ++i) {
    self->data[i] = str_dup(from->data[i]);
  }
  return false;
}

static bool
str_dict_copy(str_dict_t* self, const str_dict_t* from) 
{
  if (self == NULL) return false;
  str_dict_null(self);
  self->size = from->size;
  self->data = malloc(from->size * sizeof(str_dict_entry_t));
  for (ssize_t i=0; i < self->size; ++i) {
    self->data[i].key = str_dup(from->data[i].key);
    self->data[i].value = str_dup(from->data[i].value);
  }
  return true;
}

static bool
str_list_dict_copy(str_list_dict_t* self, const str_list_dict_t* from) 
{
  if (self == NULL) return false;
  str_list_dict_null(self);
  self->size = from->size;
  self->data = malloc(from->size * sizeof(str_list_dict_entry_t));
  for (ssize_t i=0; i < self->size; ++i) {
    self->data[i].key = str_dup(from->data[i].key);
    str_list_init(&self->data[i].value);
    str_list_copy(&self->data[i].value, &from->data[i].value);
  }
  return true;
}

static bool
str_list_dict_dict_copy(str_list_dict_dict_t* self, const str_list_dict_dict_t* from) 
{
  if (self == NULL) return false;
  str_list_dict_dict_null(self);
  self->size = from->size;
  self->data = malloc(from->size * sizeof(str_list_dict_entry_t));
  for (ssize_t i=0; i < self->size; ++i) {
    self->data[i].key = str_dup(from->data[i].key);
    str_list_dict_init(&self->data[i].value);
    str_list_dict_copy(&self->data[i].value, &from->data[i].value);
  }
  return true;
}

/* dup */

static str_t restrict
str_dup4(const str_t str1, const str_t str2, const str_t str3, const str_t str4)
{
  str_t res = NULL;
  str_list_entry_t data[] = { str1, str2, str3, str4 };
  str_list_t list = { 4, data };
  str_copy_str_list(&res, &list);
  return res;
}

static str_t restrict
str_dup3(const str_t str1, const str_t str2, const str_t str3)
{
   return str_dup4(str1, str2, str3, NULL);
}

static str_t restrict
str_dup2(const str_t str1, const str_t str2)
{
   return str_dup4(str1, str2, NULL, NULL);
}

static str_list_t*
str_list_dup(const str_list_t* self)
{
  str_list_t* into = NULL;
  if (self == NULL) return into;
  into = str_list_new();
  str_list_copy(into, self);
  return into;
}

static str_dict_t*
str_dict_dup(const str_dict_t* self)
{
  str_dict_t* into = NULL;
  if (self == NULL) return into;
  into = str_dict_new();
  str_dict_copy(into, self);
  return into;
}

static str_list_dict_t*
str_list_dict_dup(const str_list_dict_t* self)
{
  str_list_dict_t* into = NULL;
  if (self == NULL) return into;
  into = str_list_dict_new();
  str_list_dict_copy(into, self);
  return into;
}

static str_list_dict_dict_t*
str_list_dict_dict_dup(const str_list_dict_dict_t* self)
{
  str_list_dict_dict_t* into = NULL;
  if (self == NULL) return into;
  into = str_list_dict_dict_new();
  str_list_dict_dict_copy(into, self);
  return into;
}

/* sets */

static void
str_sets(str_t* self, str_t from) 
{
  if (from == NULL) return;
  if (self == NULL) return;
  if (*self) free(*self);
  *self = from;
}

static void
str_set(str_t* self, const str_t from) 
{
  str_sets(self, str_dup(from));
}

static void
str_list_sets(str_list_t* self, str_list_t* from)
{
  str_list_null(self);
  if (from) {
    self->size = from->size;
    self->data = from->data;
    from->size = 0;
    from->data = NULL;
    free(from);
  }
}

static void
str_list_set(str_list_t* self, const str_list_t* from) 
{
  str_list_sets(self, str_list_dup(from));
}

static void
str_dict_sets(str_dict_t* self, str_dict_t* from)
{
  str_dict_null(self);
  if (from) {
    self->size = from->size;
    self->data = from->data;
    from->size = 0;
    from->data = NULL;
    free(from);
  }
}

static void
str_dict_set(str_dict_t* self, const str_dict_t* from) 
{
  str_dict_sets(self, str_dict_dup(from));
}

static void
str_list_dict_sets(str_list_dict_t* self, str_list_dict_t* from)
{
  str_list_dict_null(self);
  if (from) {
    self->size = from->size;
    self->data = from->data;
    from->size = 0;
    from->data = NULL;
    free(from);
  }
}

static void
str_list_dict_set(str_list_dict_t* self, const str_list_dict_t* from) 
{
  str_list_dict_sets(self, str_list_dict_dup(from));
}

static void
str_list_dict_dict_sets(str_list_dict_dict_t* self, str_list_dict_dict_t* from)
{
  str_list_dict_dict_null(self);
  if (from) {
    self->size = from->size;
    self->data = from->data;
    from->size = 0;
    from->data = NULL;
    free(from);
  }
}

static void
str_list_dict_dict_set(str_list_dict_dict_t* self, const str_list_dict_dict_t* from) 
{
  str_list_dict_dict_sets(self, str_list_dict_dict_dup(from));
}


/* add */

static void
str_adds(str_t* self, str_t value)
{
  if (! self) return;
  if (!value) return;
  if (! *self) {
      *self = value;
      return;
  }
  ssize_t len1 = str_len(*self);
  ssize_t len2 = str_len(value);
  *self = realloc(*self, len1 + len2 + 1);
  memcpy(*self + len1, value, len2+1);
  str_free(value);
  return;
}

static void
str_add(str_t* self, str_t value)
{
  if (! self) return;
  if (!value) return;
  if (! *self) {
      *self = str_dup(value);
      return;
  }
  ssize_t len1 = str_len(*self);
  ssize_t len2 = str_len(value);
  *self = realloc(*self, len1 + len2 + 1);
  memcpy(*self + len1, value, len2 + 1);
  return;
}

static void
str_list_adds(str_list_t* self, str_t value)
{
  self->size += 1;
  self->data = realloc(self->data, self->size * sizeof(str_t));
  self->data[self->size-1] = value;
}

static void
str_list_add(str_list_t* self, const str_t value)
{
    str_list_adds(self, str_dup(value));
}

static void
str_list_adds_all(str_list_t* self, str_list_t* value)
{
  if (value == NULL || value->size == 0) return;
  ssize_t oldsize = self->size;
  self->size += value->size;
  self->data = realloc(self->data, self->size * sizeof(str_t));
  for (int i=0; i < value->size; ++i) {
     self->data[oldsize+i] = value->data[i];
     value->data[i] = NULL;
  }
  str_list_free(value);
}

static void
str_list_add_all(str_list_t* self, const str_list_t* value)
{
    str_list_adds_all(self, str_list_dup(value));
}

static void
str_list_list_adds(str_list_list_t* self, str_list_t* value)
{
  self->size += 1;
  self->data = realloc(self->data, self->size * sizeof(str_list_t));
  self->data[self->size-1].size = value->size;
  self->data[self->size-1].data = value->data;
  value->size = 0;
  value->data = NULL;
  str_list_free(value);
}

static void
str_list_list_add(str_list_list_t* self, const str_list_t* value)
{
    str_list_list_adds(self, str_list_dup(value));
}

static void
str_list_list_add4(str_list_list_t* self, str_t str1, str_t str2, str_t str3, str_t str4)
{
    str_list_entry_t data[] = { str1, str2, str3, str4 };
    str_list_t value = { 4, data };
    str_list_list_add(self, &value);
}

static void
str_list_list_add3(str_list_list_t* self, str_t str1, str_t str2, str_t str3)
{
    str_list_entry_t data[] = { str1, str2, str3 };
    str_list_t value = { 3, data };
    str_list_list_add(self, &value);
}

static void
str_list_list_add2(str_list_list_t* self, str_t str1, str_t str2)
{
    str_list_entry_t data[] = { str1, str2 };
    str_list_t value = { 2, data };
    str_list_list_add(self, &value);
}

static void
str_list_list_add1(str_list_list_t* self, str_t str1)
{
    str_list_entry_t data[] = { str1 };
    str_list_t value = { 1, data };
    str_list_list_add(self, &value);
}


static void
str_dict_adds(str_dict_t* self, const str_t key, str_t value)
{
  if (! key) {
      if (value)
         str_free (value);
      return;
  }
  ssize_t pos = str_dict_find_pos(self, key);
  if (pos < self->size && str_equal(self->data[pos].key, key)) {
      str_sets(&self->data[pos].value, value);
      return;
  }
  self->size += 1;
  self->data = realloc(self->data, self->size * sizeof(str_dict_entry_t));
  for (ssize_t i=self->size-1; i > pos; --i) {
    self->data[i].key = self->data[i-1].key;
    self->data[i].value = self->data[i-1].value;
  }
  self->data[pos].key = str_dup(key);
  self->data[pos].value = value;
  ///logg_info_str_dict("str_dict_adds", self);
}

static void
str_dict_add(str_dict_t* self, const str_t key, const str_t value)
{
    str_dict_adds(self, key, str_dup(value));
}


static void
str_list_dict_adds(str_list_dict_t* self, const str_t key, str_list_t* value)
{
  if (! key) {
      if (value) 
         str_list_free (value);
      return;
  }
  ssize_t pos = str_list_dict_find_pos(self, key);
  if (pos < self->size && str_equal(self->data[pos].key, key)) {
      str_list_adds_all(&self->data[pos].value, value);
      return;
  }
  self->size += 1;
  self->data = realloc(self->data, self->size * sizeof(str_list_dict_entry_t));
  for (ssize_t i=self->size-1; i > pos; --i) {
    self->data[i].key = self->data[i-1].key;
    self->data[i].value = self->data[i-1].value;
  }
  self->data[pos].key = str_dup(key);
  self->data[pos].value.size = value->size;
  self->data[pos].value.data = value->data;
  value->size = 0;
  value->data = NULL;
  str_list_free(value);
  // str_list_init(&self->data[pos].value);
  // str_list_sets(&self->data[pos].value, value);
  if (pos > 0 && 0 > str_cmp(self->data[pos].key, self->data[pos-1].key)) {
      logg_info("new pos[%i] '%s' is smaller than pos[%i-1] '%s'", 
         pos, self->data[pos].key, pos, self->data[pos-1].key);
      logg_info_str_list_dict("str_list_dict_adds", self);
  } 
  if (pos < self->size-1 && 0 < str_cmp(self->data[pos].key, self->data[pos+1].key)) {
      logg_info("new pos[%i] '%s' is bigger than pos[%i+1] '%s'", 
         pos, self->data[pos].key, pos, self->data[pos+1].key);
      logg_info_str_list_dict("str_list_dict_adds", self);
  } 

}

static void
str_list_dict_add(str_list_dict_t* self, const str_t key, const str_list_t* value)
{
    str_list_dict_adds(self, key, str_list_dup(value));
}

static void
str_list_dict_add1(str_list_dict_t* self, const str_t key, str_t value)
{
    str_t data[] = { value };
    str_list_t list = { 1, data };
    str_list_dict_add(self, key, &list);
}

static void
str_list_dict_adds1(str_list_dict_t* self, const str_t key, str_t value)
{
    str_list_t* list = str_list_new0(1);
    list->data[0] = value;
    str_list_dict_adds(self, key, list);
}

static void
str_list_dict_dict_adds(str_list_dict_dict_t* self, const str_t key, str_list_dict_t* value)
{
  if (! key) {
      if (value) 
          str_list_dict_free (value);
      return;
  }
  ssize_t pos = str_list_dict_dict_find_pos(self, key);
  if (pos < self->size && str_equal(self->data[pos].key, key)) {
      str_list_dict_sets(&self->data[pos].value, value);
      return;
  }
  self->size += 1;
  self->data = realloc(self->data, self->size * sizeof(str_list_dict_dict_entry_t));
  for (ssize_t i=self->size-1; i > pos; --i) {
    self->data[i].key = self->data[i-1].key;
    self->data[i].value = self->data[i-1].value;
  }
  self->data[pos].key = str_dup(key);
  self->data[pos].value.size = value->size;
  self->data[pos].value.data = value->data;
  value->size = 0;
  value->data = NULL;
  str_list_dict_free(value);
  if (pos > 0 && 0 > str_cmp(self->data[pos].key, self->data[pos-1].key)) {
      logg_info("new pos[%i] '%s' is smaller than pos[%i-1] '%s'", 
         pos, self->data[pos].key, pos, self->data[pos-1].key);
      logg_info_str_list_dict_dict("str_list_dict_dict_adds", self);
  } 
  if (pos < self->size-1 && 0 < str_cmp(self->data[pos].key, self->data[pos+1].key)) {
      logg_info("new pos[%i] '%s' is bigger than pos[%i+1] '%s'", 
         pos, self->data[pos].key, pos, self->data[pos+1].key);
      logg_info_str_list_dict_dict("str_list_dict_dict_adds", self);
  } 
}

static void
str_list_dict_dict_add(str_list_dict_dict_t* self, const str_t key, const str_list_dict_t* value)
{
    str_list_dict_dict_adds(self, key, str_list_dict_dup(value));
}

static void
ptr_dict_adds(ptr_dict_t* self, const str_t key, void* value)
{
  if (! key) {
      if (value)
         str_free (value);
      return;
  }
  ssize_t pos = ptr_dict_find_pos(self, key);
  ptr_dict_t old = { self->size, self->data };
  self->size = old.size +1;
  self->data = malloc(self->size * sizeof(ptr_dict_entry_t));
  for (ssize_t i=0; i < pos; ++i) {
    self->data[i].key = old.data[i].key;
    self->data[i].value = old.data[i].value;
  }
  self->data[pos].key = str_dup(key);
  self->data[pos].value = value;
  for (ssize_t i=pos; i < old.size; ++i) {
    self->data[i+1].key = old.data[i].key;
    self->data[i+1].value = old.data[i].value;
  }
  free(old.data);
}

static void
ptr_dict_add(ptr_dict_t* self, const str_t key, void* value)
{
    ptr_dict_adds(self, key, str_dup(value));
}

/* startswith */

static bool
str_startswith(const str_t self, const str_t key) 
{
  if (self == NULL) return key == NULL;
  if (key == NULL) return false;
  str_t found = strstr(self, key);
  return found == self;
}

static bool
str_endswith(const str_t self, const str_t key)
{
  if (self == NULL) return key == NULL;
  if (key == NULL) return false;
  ssize_t key_len = str_len(key);
  ssize_t buf_len = str_len(self);
  if (key_len > buf_len) return false;
  return !strcmp(self + (buf_len - key_len), key);
}

/* del */

static void
str_list_del(str_dict_t* self, const ssize_t pos)
{
    if (pos >=0) {
        str_dict_t old = str_dict_NULL;
        str_dict_sets(&old, self);
        self->size = old.size -1;
        self->data = malloc(self->size * sizeof(str_dict_entry_t));
        for (ssize_t i=0; i < pos; ++i) {
            self->data[i].key = old.data[i].key;
            self->data[i].value = old.data[i].value;
        }
        for (ssize_t i=pos; i < self->size; ++i) {
            self->data[i].key = old.data[i+1].key;
            self->data[i].value = old.data[i+1].value;
        }
        if (old.data[pos].key) {
            free(old.data[pos].key);
        }
        if (old.data[pos].value) {
            free(old.data[pos].value);
        }
        free(old.data);
    }
}

static void
str_dict_del(str_dict_t* self, const str_t key)
{
    ssize_t pos = str_dict_find(self, key);
    if (pos >=0) {
        str_dict_t old = str_dict_NULL;
        str_dict_sets(&old, self);
        self->size = old.size -1;
        self->data = malloc(self->size * sizeof(str_dict_entry_t));
        for (ssize_t i=0; i < pos; ++i) {
            self->data[i].key = old.data[i].key;
            self->data[i].value = old.data[i].value;
        }
        for (ssize_t i=pos; i < self->size; ++i) {
            self->data[i].key = old.data[i+1].key;
            self->data[i].value = old.data[i+1].value;
        }
        if (old.data[pos].key) {
            free(old.data[pos].key);
        }
        if (old.data[pos].value) {
            free(old.data[pos].value);
        }
        free(old.data);
    }
}

/* cut */

static str_t restrict
str_cut(const str_t self, ssize_t a, ssize_t b) {
  if (self == NULL) return NULL;
  ssize_t size = str_len(self);
  if (a < 0) a = size + a;
  if (b < 0) b = size + b;
  if (a < 0 || b < 0 || a >= size || b < a)
    return str_dup("");
  if (b >= size) b = size+1;
  ssize_t s = b-a;
  str_t res = malloc(s+1);
  memcpy(res, self+a, s);
  res[s] = '\0';
  return res;
}

#define STR_END SSIZE_MAX
#define STR_LIST_END SSIZE_MAX

static str_t restrict
str_cut_end(const str_t self, ssize_t a) {
  return str_cut(self, a, STR_END);
}

static str_list_t* restrict
str_list_cut(const str_list_t* self, ssize_t a, ssize_t b)
{
  if (self == NULL) return NULL;
  str_list_t* res = malloc(sizeof(str_list_t));
  str_list_init(res);
  if (a < 0) a = self->size + a;
  if (b < 0) b = self->size + b;
  if (a < 0 || b < 0 || a >= self->size || b < a)
    return res;
  if (b >= self->size) b = self->size+1;
  ssize_t s = b-a;
  res->size = s;
  res->data = malloc(res->size * sizeof(str_t));
  for (ssize_t i = 0; i < s; ++i) {
    res->data[i] = str_dup(self->data[a+i]);
  }
  return res;
}

static str_list_t* restrict
str_list_cut_end(const str_list_t* self, ssize_t a) {
  return str_list_cut(self, a, STR_LIST_END);
}

static char str_delim[] = " \r\n\f";

/* empty */

static bool
str_empty(const str_t self) 
{
  if (self == NULL) return true;
  return 0 == str_len(self);
}

static bool
str_list_empty(const str_list_t* self) 
{
  if (self == NULL) return true;
  return 0 == self->size;
}

static bool
str_dict_empty(const str_dict_t* self) 
{
  if (self == NULL) return true;
  return 0 == self->size;
}

static bool
str_list_dict_empty(const str_list_dict_t* self) 
{
  if (self == NULL) return true;
  return 0 == self->size;
}

/* strip */

static str_t restrict
str_lstrip(const str_t self) 
{
  ssize_t size = str_len(self);
  for (ssize_t i = 0; i < size; ++i) {
    if (strchr(str_delim, self[i])) 
      continue;
    return str_cut(self, i, size);
  }
  return str_dup("");
}

static str_t restrict
str_strip(const str_t self) 
{
  ssize_t size = str_len(self);
  for (ssize_t i = 0; i < size; ++i) {
    if (strchr(str_delim, self[i]))
      continue;
    for (ssize_t j = size; j >= i; --j) {
      if (strchr(str_delim, self[j]))
        continue;
      return str_cut(self, i, j+1);
    }
  }
  return str_dup("");
}

static str_t restrict
str_rstrip(const str_t self) 
{
  ssize_t size = str_len(self);
  for (ssize_t j = size; j >= 0; --j) {
    if (strchr(str_delim, self[j]))
      continue;
    return str_cut(self, 0, j+1);
  }
  return str_dup("");
}

/* split */
static str_list_t* restrict
str_split(const str_t text, const char delim) 
{
  str_list_t* res = str_list_new();
  ssize_t n = str_len(text);
  for (ssize_t x = 0; x < n; ++x) {
    for (; x < n; ++x) {
      if (text[x] != delim) break;
    }
    if (x == n) break;
    ssize_t a = x;
    for (; x < n; ++x) {
      if (text[x] == delim) break;
    }
    str_list_adds(res, str_cut(text, a, x));
  }
  return res;
}

/* str */

static str_t
str_join2(const str_t self, const str_t from, const str_t delim)
{
  if (!from) return str_dup(self);
  return str_dup3(self, delim, from);
}

static str_t restrict
str_list_join(const str_list_t* self, const str_t delim)
{
  str_t res = NULL;
  str_list_t join;
  str_list_init0(&join, self->size * 2);
  int x = 0;
  for (int i=0; i < self->size; ++i) {
     if (self->data[i]) {
       if (x) {
         join.data[x] = str_dup(delim);
         ++x;
       }
       join.data[x] = str_dup(self->data[i]);
       ++x;
     }
  }
  join.size = x;
  str_copy_str_list(&res, &join);
  str_list_null(&join);
  return res;
}

static str_t restrict
str_list3_join(str_t str1, str_t str2, str_t str3, const str_t delim)
{
   str_list_entry_t data[] = { str1, str2, str3 };
   str_list_t list = { 3, data };
   return str_list_join(&list, delim);
}

/* format */
static str_t
str_format(const char* format, ...) {
  str_t result;
  va_list ap;
  int size;
  /* determine size */
  va_start(ap, format);
  size = vsnprintf(NULL, 0, format, ap);
  va_end(ap);
  str_init0(&result, size);
  /* format buffer */
  va_start(ap, format);
  size = vsnprintf(result, size+1, format, ap);
  va_end(ap);
  if (size < 0) {
    str_null(&result);
  }
  return result;
}

/* os.path functions */

str_t restrict
os_path_join(str_t path, str_t filename)
{
    return str_dup3(path, "/", filename);
}

bool
os_path_isdir(str_t path)
{
    struct stat st;
    stat(path, &st);
    return (st.st_mode & S_IFMT) == S_IFDIR;
}

bool
os_path_islink(str_t path)
{
    struct stat st;
    stat(path, &st);
    return (st.st_mode & S_IFMT) == S_IFLNK;
}

str_list_t* restrict
os_path_listdir(str_t path)
{
    str_list_t* names = str_list_new();
    DIR* dirp = opendir(path);
    while (dirp) {
       struct dirent* d = readdir(dirp);
       if (d) {
           str_list_add(names, d->d_name);
       } else {
          closedir(dirp);
          dirp = NULL;
          break;
       }
    }
    return names;
}

str_list_t* restrict
os_listdir(str_t path)
{
    return os_path_listdir(path);
}

str_t restrict
os_path_basename(str_t path)
{
    char* found = strrchr(path, '/');
    if (found) {
        return str_dup(found);
    }
    return str_dup(path);
}