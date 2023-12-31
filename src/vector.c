/* Largely from https://github.com/glankk/n64/blob/master/include/vector/vector.c */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vector.h"

void
vector_new (Vector* vector, size_t elemSize)
{
    vector->elemSize = elemSize;
    vector->limit = vector->capacity = 0;
    vector->start = vector->end = NULL;
    vector->rstart = vector->rend = NULL;
}

int
vector_destroy (Vector* vector)
{
    if (vector->start != NULL)
        free(vector->start);
    return 0;
}

void*
vector_at (const Vector* vector, size_t pos)
{
    if (vector->start == NULL || pos >= vector->limit)
        return NULL;
    return (uint8_t*)vector->start + vector->elemSize * pos;
}

int
vector_reserve (Vector* vector, size_t num)
{
    uint8_t* newData;
    size_t newCapacity = vector->limit + num;

    if (newCapacity <= vector->capacity)
        return 1;

    newData = realloc(vector->start, vector->elemSize * newCapacity);

    if (newData == NULL)
        return -1;

    vector->start = newData;
    vector->rend = newData - vector->elemSize;
    vector->end = (uint8_t*)vector->start + vector->elemSize * vector->limit;
    vector->rstart = (uint8_t*)vector->end - vector->elemSize;
    vector->capacity = newCapacity;
    return 0;
}

void*
vector_insert (Vector* vector, size_t position, size_t num, const void* data)
{
    size_t newCapacity;

    if (num == 0 || position > vector->limit)
        return NULL;

    newCapacity = vector->capacity;

    if (newCapacity == 0)
        newCapacity = num;
    else
    {
        if (newCapacity < vector->limit + num)
            newCapacity *= 2;
        if (newCapacity < vector->limit + num)
            newCapacity = vector->limit + num;
    }

    if (newCapacity != vector->capacity)
    {
        uint8_t* newData = realloc(vector->start, vector->elemSize * newCapacity);

        if (newData == NULL)
            return NULL;

        vector->start = newData;
        vector->rend = newData - vector->elemSize;
        vector->capacity = newCapacity;
    }

    memmove((uint8_t*)vector->start + vector->elemSize * (position + num),
            (uint8_t*)vector->start + vector->elemSize * position,
            vector->elemSize * (vector->limit - position));

    if (data != NULL)
        memcpy((uint8_t*)vector->start + vector->elemSize * position, data, vector->elemSize * num);

    vector->limit += num;
    vector->end = (uint8_t*)vector->start + vector->elemSize * vector->limit;
    vector->rstart = (uint8_t*)vector->end - vector->elemSize;

    return (uint8_t*)vector->start + vector->elemSize * position;
}

void*
vector_push_back (Vector* vector, size_t num, const void* data)
{
    return vector_insert(vector, vector->limit, num, data);
}

int
vector_delete (Vector* vector, size_t position, size_t num)
{
    if (vector->start == NULL || num > vector->limit || position >= vector->limit)
        return -1;

    if (num == vector->limit)
    {
        vector_clear(vector);
    }
    else
    {
        memmove((uint8_t*)vector->start + vector->elemSize * position,
                (uint8_t*)vector->start + vector->elemSize * (position + num),
                vector->elemSize * (vector->limit - position - num));
        vector->limit -= num;
        vector->end = (uint8_t*)vector->start + vector->elemSize * vector->limit;
        vector->rstart = (uint8_t*)vector->end - vector->elemSize;
    }
    return 0;
}

int
vector_resize (Vector* vector)
{
    size_t newCapacity = vector->limit;
    uint8_t* newData = realloc(vector->start, vector->elemSize * newCapacity);

    if (newCapacity > 0 && newData == NULL)
        return -1;

    vector->start = newData;
    vector->rend = newData - vector->elemSize;
    vector->end = (uint8_t*)vector->start + vector->elemSize * vector->limit;
    vector->rstart = (uint8_t*)vector->end - vector->elemSize;
    vector->capacity = newCapacity;
    return 0;
}

void*
vector_release (Vector* vector)
{
    void* data = vector->start;
    vector_new(vector, vector->elemSize);
    return data;
}

void
vector_clear (Vector* vector)
{
    vector->limit = 0;
    vector->end = vector->start;
    vector->rstart = vector->start;
    vector->rend = vector->start;
}
