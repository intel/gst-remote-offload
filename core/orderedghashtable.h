/*
 *  orderedghashtable.h - Ordered GHashTable object
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: Metcalfe, Ryan <ryan.d.metcalfe@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 *
 *  GHashTable's are useful for their fast find abilities. But in
 *   some cases, one would like to iterate through all of the key/value
 *   pairs and perform some function for each one. In cases where ordering
 *   of these operations matters, iterating directly using the GHashTable
 *   is dangerous as there isn't any guarantee for the order in which GHashTable
 *   internally stores these values.
 *
 *  This object is written to provide support for both fast find operations,
 *   and deterministic iterators. It wraps both a GHashTable and GArray. For
 *   find / insertion, it primarily uses the underlying GHashTables.
 *   For the iterator support, it uses the underlying GArray's, in which
 *   the order of key/value are given in the same order of (initial) insertion.
 *
 *   Not MT-safe.
 */
#ifndef __ORDERED_GHASHTABLE_H__
#define __ORDERED_GHASHTABLE_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _OrderedGHashTable OrderedGHashTable;

OrderedGHashTable *ordered_g_hash_table_new(GHashFunc       hash_func,
                                            GEqualFunc      key_equal_func,
                                            GDestroyNotify  key_destroy_func,
                                            GDestroyNotify  value_destroy_func);

//Does hash table already contain key?
gboolean ordered_g_hash_table_contains (OrderedGHashTable *hash_table,
                                        gconstpointer key);

gpointer    ordered_g_hash_table_lookup(OrderedGHashTable     *hash_table,
                                            gconstpointer   key);

guint       ordered_g_hash_table_size (OrderedGHashTable     *hash_table);

//Insert new key/value pair
gboolean  ordered_g_hash_table_insert(OrderedGHashTable  *hash_table,
                                      gpointer        key,
                                      gpointer        value);

//Call GHFunc for each key/value pair.
void ordered_g_hash_table_foreach (OrderedGHashTable *hash_table,
                                   GHFunc func,
                                   gpointer user_data);

typedef struct _OrderedGHashTableIter OrderedGHashTableIter;

struct _OrderedGHashTableIter
{
  /*< private >*/
  guint dummy1;
  gpointer dummy2;
  gboolean dummy3;
};

void ordered_g_hash_table_iter_init(OrderedGHashTableIter *iter,
                                    OrderedGHashTable     *hash_table);

gboolean    ordered_g_hash_table_iter_next (OrderedGHashTableIter *iter,
                                            gpointer       *key,
                                            gpointer       *value);

//destroy using g_object_unref()

G_END_DECLS

#endif /* __ORDERED_GHASHTABLE_H__ */
