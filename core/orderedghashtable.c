/*
 *  orderedghashtable.c - Ordered GHashTable object
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
#include "orderedghashtable.h"

#define ORDEREDGHASHTABLE_TYPE (ordered_ghash_table_get_type ())
G_DECLARE_FINAL_TYPE (OrderedGHashTable, ordered_ghash_table, ORDERED, GHASHTABLE, GObject)


typedef struct {
   gpointer key;
   gpointer val;
}PairEntry;

typedef struct {
   GHashTable *hash;
   GArray *pairarray;
   GEqualFunc key_equal_func;
}OrderedGHashTablePrivate;

struct _OrderedGHashTable
{
  GObject parent_instance;

  /* Other members, including private data. */
  OrderedGHashTablePrivate priv;
};

G_DEFINE_TYPE (OrderedGHashTable, ordered_ghash_table, G_TYPE_OBJECT)

static void
ordered_ghash_table_finalize (GObject *gobject)
{
  OrderedGHashTable *self = ORDERED_GHASHTABLE(gobject);

  if( self->priv.hash )
  {
     g_hash_table_destroy(self->priv.hash);
  }

  if( self->priv.pairarray )
  {
     g_array_free(self->priv.pairarray, TRUE);
  }

  G_OBJECT_CLASS (ordered_ghash_table_parent_class)->finalize (gobject);
}

static void
ordered_ghash_table_class_init (OrderedGHashTableClass *klass)
{
   GObjectClass *object_class = G_OBJECT_CLASS (klass);

   object_class->finalize = ordered_ghash_table_finalize;
}

static void
ordered_ghash_table_init (OrderedGHashTable *self)
{
   self->priv.hash = NULL;
   self->priv.pairarray = g_array_new(FALSE, FALSE, sizeof(PairEntry));
}

OrderedGHashTable *ordered_g_hash_table_new(GHashFunc       hash_func,
                                            GEqualFunc      key_equal_func,
                                            GDestroyNotify  key_destroy_func,
                                            GDestroyNotify  value_destroy_func)
{
   OrderedGHashTable *oghash = g_object_new(ORDEREDGHASHTABLE_TYPE, NULL);

   if( oghash )
   {
      oghash->priv.key_equal_func = key_equal_func;
      oghash->priv.hash = g_hash_table_new_full(hash_func,
                                                key_equal_func,
                                                key_destroy_func,
                                                value_destroy_func);

      if( !oghash->priv.hash )
      {
         g_object_unref(oghash);
         oghash = NULL;
      }
   }

   return oghash;
}

gboolean  ordered_g_hash_table_insert(OrderedGHashTable   *hash_table,
                                      gpointer        key,
                                      gpointer        value)
{
   if( !ORDERED_IS_GHASHTABLE(hash_table) ) return FALSE;

   gboolean ret = g_hash_table_insert(hash_table->priv.hash,
                                      key,
                                      value);

   //if key didn't exist yet
   if( ret )
   {
      PairEntry entry = {key, value};
      g_array_append_val(hash_table->priv.pairarray,
                         entry);
   }
   else
   {
      //find the current value we are storing for key, and update
      // it's value.
      //TODO: This can be optimized. But, for the use-case this
      // object is being designed to serve, we aren't expecting
      // redundant keys to be inserted very often (or at all),
      // so just take the hit for now.
      if( hash_table->priv.key_equal_func )
      {
         for( guint index = 0;
              index < hash_table->priv.pairarray->len;
              index++ )
         {
            PairEntry *entry = &g_array_index(hash_table->priv.pairarray,
                                              PairEntry,
                                              index);

            if( hash_table->priv.key_equal_func(entry->key,
                                                key) )
            {
               entry->val = value;
               break;
            }
         }
      }
   }

   return ret;
}

gboolean ordered_g_hash_table_contains (OrderedGHashTable *hash_table,
                                        gconstpointer key)
{
   if( !ORDERED_IS_GHASHTABLE(hash_table) ) return FALSE;

   return g_hash_table_contains(hash_table->priv.hash,
                                key);
}

gpointer ordered_g_hash_table_lookup(OrderedGHashTable     *hash_table,
                                     gconstpointer   key)
{
   if( !ORDERED_IS_GHASHTABLE(hash_table) ) return NULL;

   return g_hash_table_lookup(hash_table->priv.hash,
                             key);
}

guint ordered_g_hash_table_size (OrderedGHashTable *hash_table)
{
   if( !ORDERED_IS_GHASHTABLE(hash_table) ) return 0;

   return g_hash_table_size(hash_table->priv.hash);
}

void ordered_g_hash_table_foreach (OrderedGHashTable *hash_table,
                           GHFunc func,
                           gpointer user_data)
{
   if( !ORDERED_IS_GHASHTABLE(hash_table) ) return;

   for( guint index = 0;
              index < hash_table->priv.pairarray->len;
              index++ )
    {
      PairEntry *entry = &g_array_index(hash_table->priv.pairarray,
                                              PairEntry,
                                              index);

      if( func )
         func(entry->key, entry->val, user_data);
    }
}

void ordered_g_hash_table_iter_init(OrderedGHashTableIter *iter,
                                    OrderedGHashTable     *hash_table)
{
   if( !iter || !ORDERED_IS_GHASHTABLE(hash_table) ) return;

   iter->dummy1 = 0;
   iter->dummy2 = hash_table;
   iter->dummy3 = TRUE;
}

gboolean  ordered_g_hash_table_iter_next (OrderedGHashTableIter *iter,
                                          gpointer       *key,
                                          gpointer       *value)
{
  if( !iter || !ORDERED_IS_GHASHTABLE(iter->dummy2)
      || !key || !value ) return FALSE;

  OrderedGHashTable *hash_table = ORDERED_GHASHTABLE(iter->dummy2);

  if( iter->dummy3 )
  {
     iter->dummy3 = FALSE;
     iter->dummy1 = 0;
  }
  else
  {
     iter->dummy1++;
  }

  if( iter->dummy1 >= hash_table->priv.pairarray->len )
     return FALSE;


  PairEntry *entry = &g_array_index(hash_table->priv.pairarray,
                                    PairEntry,
                                    iter->dummy1);

  *key = entry->key;
  *value = entry->val;

  return TRUE;
}

