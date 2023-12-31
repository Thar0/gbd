#ifndef OBSTACK_H_
#define OBSTACK_H_

#include "vector.h"
#include "macros.h"

typedef struct {
    Vector v;
} ObStack;

static ALWAYS_INLINE void
obstack_new (ObStack *obstack, size_t elem_size)
{
    vector_new(&obstack->v, elem_size);
}

static ALWAYS_INLINE int
obstack_free (ObStack *obstack)
{
    return vector_destroy(&obstack->v);
}

static ALWAYS_INLINE void *
obstack_push (ObStack *obstack, const void *ob)
{
    return vector_push_back(&obstack->v, 1, ob);
}

static ALWAYS_INLINE int
obstack_pop (ObStack *obstack, int n)
{
    return vector_delete(&obstack->v, obstack->v.limit - 1, 1);
}

static ALWAYS_INLINE void *
obstack_peek (ObStack *obstack)
{
    return vector_at(&obstack->v, obstack->v.limit - 1);
}

#endif
