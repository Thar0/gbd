#ifndef VECTOR_H_
#define VECTOR_H_

#include <stddef.h>

typedef struct Vector {
    size_t elemSize;
    size_t limit;
    size_t capacity;
    void  *start;
    void  *end;
    void  *rstart;
    void  *rend;
} Vector;

#define VECTOR_FOR_EACH_ELEMENT(vector, element) \
    for ((element) = (vector)->start; (element) != (vector)->end; (element)++)

#define VECTOR_FOR_EACH_ELEMENT_REVERSED(vector, element, type) \
    for (type(element) = (type)((vector)->end) - 1; (element) != (type)((vector)->start) - 1; (element)--)

void
vector_new(Vector *vector, size_t elemSize);

int
vector_destroy(Vector *vector);

void *
vector_at(const Vector *vector, size_t pos);

int
vector_reserve(Vector *vector, size_t num);

void *
vector_insert(Vector *vector, size_t position, size_t num, const void *data);

void *
vector_push_back(Vector *vector, size_t num, const void *data);

int
vector_delete(Vector *vector, size_t position, size_t num);

int
vector_resize(Vector *vector);

void *
vector_release(Vector *vector);

void
vector_clear(Vector *vector);

#endif
