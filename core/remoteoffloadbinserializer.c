/*
 *  remoteoffloadbinserializer.c - RemoteOffloadBinSerializer object
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
 */
#include "remoteoffloadbinserializer.h"
#include "remoteoffloadelementserializer.h"
#include "orderedghashtable.h"

//Example, Bin Description for 3 elements, 6 pads
//
//                                bin
// [->(sink0)(element0)(src1)->(sink2)(element1)(src3)->(sink4)(element2)(src5)->]
//
//[BinDescriptionHeader]
//       ^              [ElementDescriptionHeader (element0)]
//       |                         ^
//       |                         |
//       |              properties_description_size (element0)
//       |                         |
//       |                         v
//       |              [ElementDescriptionHeader (element1)]
//       |                         ^
//       |                         |
//    total_size                   |
//       |              properties_description_size (element1)
//       |                         |
//       |                         |
//       |                         v
//       |              [ElementDescriptionHeader (element2)]
//       |                         ^
//       |                         |
//       |              properties_description_size (element2)
//       |                         |
//       |                         v
//       |              [PadDescription (sink0) ]
//       |              [PadDescription (src1)  ]
//       |              [PadDescription (sink2) ]
//       |              [PadDescription (src3)  ]
//       |              [PadDescription (sink4) ]
//       v              [PadDescription (src5)  ]


typedef struct _BinDescriptionHeader
{
  gchar binname[128];
  guint32 nelements; //the number of Element Descriptions
  guint32 npads;
}BinDescriptionHeader;

enum PadType
{
   BINDESC_PADTYPE_SINK = 0,
   BINDESC_PADTYPE_SRC
};

typedef struct _PadDescription
{
   gchar padname[128];
   guint32 pad_type; //the type of pad (sink / src)
   guint32 pad_presence; //GST_PAD_ALWAYS=0, GST_PAD_SOMETIMES=1, GST_PAD_REQUEST=2
   guint32 element_id; //the unique element-id of the element for which this pad belongs
   guint32 pad_id;  //a unique-id for this pad
   guint32 connected_pad_id; //the unique pad-id of the pad which this pad is linked to.
                         //If the connected_pad_id is not defined in some other
                         // PadDescription, this means that the pad is linked
                         // to an element outside of this bin, in which case,
                         // special remoteoffloadingress/remoteoffloadegress elements need to be
                         // inserted for data transfer between host and target.
}PadDescription;

struct _RemoteOffloadBinSerializer
{
   GObject parent_instance;

   RemoteOffloadElementSerializer *element_serializer;

};

GST_DEBUG_CATEGORY_STATIC (remote_offload_bin_serializer_debug);
#define GST_CAT_DEFAULT remote_offload_bin_serializer_debug

G_DEFINE_TYPE_WITH_CODE (
      RemoteOffloadBinSerializer, remote_offload_bin_serializer, G_TYPE_OBJECT,
      GST_DEBUG_CATEGORY_INIT (remote_offload_bin_serializer_debug, "remoteoffloadbinserializer", 0,
      "debug category for RemoteOffloadBinSerializer"))

static void
remote_offload_bin_serializer_finalize (GObject *gobject)
{
   RemoteOffloadBinSerializer *self = REMOTEOFFLOAD_BINSERIALIZER(gobject);

   g_object_unref(self->element_serializer);

   G_OBJECT_CLASS (remote_offload_bin_serializer_parent_class)->finalize (gobject);
}

static void
remote_offload_bin_serializer_class_init (RemoteOffloadBinSerializerClass *klass)
{
   GObjectClass *object_class = G_OBJECT_CLASS (klass);

   object_class->finalize = remote_offload_bin_serializer_finalize;
}

static void
remote_offload_bin_serializer_init (RemoteOffloadBinSerializer *self)
{
   self->element_serializer = remote_offload_element_serializer_new();
}

RemoteOffloadBinSerializer *remote_offload_bin_serializer_new()
{
   return g_object_new(REMOTEOFFLOADBINSERIALIZER_TYPE, NULL);
}

static void GFreeMemDestroy(gpointer data)
{
   g_free(data);
}

static void PadDescDestroyNotify(gpointer data)
{
   g_free(data);
}

typedef struct _ElementBlockLocation
{
   guint memBlockIndex;  //index of memBlockArray for which this property
   guint memBlockOffset; //byte offset within memBlockArray
   guint size;           //size of this block
}ElementBlockLocation;

//descbw format. For each element
//[ElementDescriptionHeader]
//guint nblocks;
//[ElementBlockLocation] x nblocks

#define SMALL_PROP_THRESHOLD 4096

enum
{
  //memory block containing BinDescriptionHeader
  MEMARRAY_INDEX_HEADER = 0,
  //memory block containing serialized bin, element, property, & pad description structures.
  MEMARRAY_INDEX_DESC,
  //memory block containing contiguous "small property" memory blocks
  MEMARRAY_INDEX_SMALL_PROP,
  MEMARRAY_INDEX_DEFAULT_MIN
};

#define ELEMENT_ID_START_INDEX 1
#define PAD_ID_START_INDEX 1

//appends each GstElement contained within the GstBin to elemArray
static gboolean populate_elem_array(GstBin *pBin, GArray *elemArray)
{
   GstIterator *it = gst_bin_iterate_elements(pBin);
   GValue item = G_VALUE_INIT;
   gboolean done = FALSE;
   gboolean ret = TRUE;
   while(!done)
   {
      switch(gst_iterator_next(it, &item))
      {
         case GST_ITERATOR_OK:
         {
            GstElement *pElem = (GstElement *)g_value_get_object(&item);
            if( !pElem ) return FALSE;

            g_array_append_val(elemArray, pElem);
         }
         break;

         case GST_ITERATOR_DONE:
            done = TRUE;
            break;

         case GST_ITERATOR_RESYNC:
            ret = FALSE;
            done = TRUE;
            break;

         case GST_ITERATOR_ERROR:
            ret = FALSE;
            done = TRUE;
            break;
      }
   }

   g_value_unset(&item);
   gst_iterator_free(it);

   return ret;
}



static gboolean serialize_elements(RemoteOffloadBinSerializer *serializer,
                                   GArray *elemArray,
                                   GstByteWriter *pDescByteWriter,
                                   GstByteWriter *pSmallPropByteWriter,
                                   GArray *memBlockArray,
                                   GHashTable *elem_to_id_map)
{
   gint32 currentelementid = ELEMENT_ID_START_INDEX;
   gboolean ret = TRUE;
   for(int i = 0; i < elemArray->len; i++ )
   {
      if( !ret ) break;

      GstElement *pElem = g_array_index(elemArray, GstElement *, i);

      gchar *name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(gst_element_get_factory(pElem)));

      ElementDescriptionHeader element_header;
      GArray *properties_mem_array = g_array_new(FALSE, FALSE, sizeof(GstMemory *));
      if( remote_offload_serialize_element(serializer->element_serializer,
                                           currentelementid, pElem,
                                           &element_header, properties_mem_array) )
      {
         gst_byte_writer_put_data (pDescByteWriter,
                                   (const guint8 *)&element_header,
                                   sizeof(element_header));
         gst_byte_writer_put_data (pDescByteWriter,
                                   (const guint8 *)&properties_mem_array->len,
                                   sizeof(properties_mem_array->len));

         for( guint memi = 0; memi < properties_mem_array->len; memi++ )
         {
            GstMemory *mem = g_array_index(properties_mem_array, GstMemory *, memi);

            gsize memblocksize = gst_memory_get_sizes(mem, NULL, NULL);
            if( !memblocksize )
            {
               GST_WARNING_OBJECT (serializer,
                                   "Element %s, memblock %u has size of 0...skipping\n",
                                   name, memi);
               continue;
            }

            GST_DEBUG_OBJECT (serializer,
                              "Element %s, memblock %u has size of %"G_GSIZE_FORMAT,
                              name, memi, memblocksize);

            //if the size of this memory block is less than SMALL_PROP_THRESHOLD,
            // i.e. if it is considered to be a "small property", merge this memory block
            // into our "small property" bytewriter stream. We take the hit of an extra copy
            // here, but probably save cycles / latency overall as compared with having to
            // send a small isolated memory block as a separate transfer.
            if( memblocksize < SMALL_PROP_THRESHOLD )
            {
               GstMapInfo mapInfo;
               if( gst_memory_map (mem, &mapInfo, GST_MAP_READ) )
               {
                  //write the location for this block to the description bw
                  ElementBlockLocation location;
                  location.memBlockIndex = MEMARRAY_INDEX_SMALL_PROP;
                  location.memBlockOffset = gst_byte_writer_get_pos(pSmallPropByteWriter);
                  location.size = mapInfo.size;
                  gst_byte_writer_put_data (pDescByteWriter,
                                            (const guint8 *)&location,
                                            sizeof(location));

                  //write this block to the 'smallprop' bytewriter
                  gst_byte_writer_put_data (pSmallPropByteWriter,
                                            (const guint8 *)mapInfo.data,
                                            mapInfo.size);

                  gst_memory_unmap (mem, &mapInfo);
               }
               else
               {
                  GST_ERROR_OBJECT (serializer,
                                    "Element %s, error mapping property mem index %u",
                                    name, memi);
                  ret = FALSE;
               }

               gst_memory_unref(mem);
            }
            else
            {
               //this memblock is large enough to stand on it's own as a separate memblock.
               // Set & append the location data to the desc bytewriter
               ElementBlockLocation location;
               location.memBlockIndex = memBlockArray->len;
               location.memBlockOffset = 0;
               location.size = memblocksize;
               gst_byte_writer_put_data (pDescByteWriter,
                                         (const guint8 *)&location,
                                         sizeof(location));

               // append this "large" memblock to the memblockarray
               g_array_append_val(memBlockArray, mem);
            }
         }

      }
      else
      {
         GST_ERROR_OBJECT (serializer,
                           "Error serializing element (name=%s)(gstelem=%p)(id=%d)",
                           name, pElem, currentelementid);
         ret = FALSE;
      }

      g_array_free(properties_mem_array, TRUE);

      g_hash_table_insert(elem_to_id_map, pElem, GINT_TO_POINTER(currentelementid));
      currentelementid++;
   }

   return ret;
}

static gboolean populate_pad_to_paddesc_map(RemoteOffloadBinSerializer *serializer,
                                            GArray *elemArray,
                                            GHashTable *elem_to_id_map,
                                            OrderedGHashTable *pad_to_padDescriptionMap)
{
   guint32 current_padId = PAD_ID_START_INDEX;

   for( int i = 0; i < elemArray->len; i++ )
   {
      GstElement *pElem = g_array_index(elemArray, GstElement *, i);

      GList *pad_list = GST_ELEMENT_PADS (pElem);
      while( pad_list != NULL )
      {
         GstPad *pad = GST_PAD_CAST(pad_list->data);
         if( gst_pad_is_linked(pad) )
         {
            PadDescription *desc = g_malloc(sizeof(PadDescription));

            gchar *padname = GST_PAD_NAME(pad);
            if( G_UNLIKELY(g_snprintf(desc->padname,
                    sizeof(desc->padname),
                    "%s", padname) > sizeof(desc->padname)) )
            {
                GST_ERROR_OBJECT (serializer,
                                  "Pad name %s is too long to fit into "
                                  "PadDescription.padname (%"G_GSIZE_FORMAT" bytes)",
                                  padname, sizeof(desc->padname));
                g_free(desc);
                return FALSE;
            }

            GstPadDirection dir = gst_pad_get_direction(pad);
            switch(dir)
            {
               case GST_PAD_SRC:
                  desc->pad_type = BINDESC_PADTYPE_SRC;
                  break;

               case GST_PAD_SINK:
                  desc->pad_type = BINDESC_PADTYPE_SINK;
                  break;

               default:
                  GST_ERROR_OBJECT (serializer, "Unknown pad direction..");
                  g_free(desc);
                  return FALSE;
                  break;
            }

            GstPadTemplate *templ = gst_pad_get_pad_template(pad);
            if( templ )
            {
               desc->pad_presence = templ->presence;
               gst_object_unref(templ);
            }
            else
            {
               //In some cases, it's expected to not be able to retrieve the template for
               // a pad. So, add an info message and assume it'll be able to be retrieved as a
               // static pad.
               GST_INFO_OBJECT (serializer,
                                "Couldn't get pad template for pad %s, from element %s\n",
                                GST_PAD_NAME(pad),
                                GST_ELEMENT_NAME(pElem));
               desc->pad_presence = GST_PAD_ALWAYS;
            }

            if( !g_hash_table_contains(elem_to_id_map, pElem) )
            {
               GST_ERROR_OBJECT (serializer, "Element not found..");
               g_free(desc);
               return FALSE;
            }

            desc->element_id = GPOINTER_TO_INT(g_hash_table_lookup(elem_to_id_map,
                                                                   pElem));
            desc->pad_id = current_padId++;

            if( !ordered_g_hash_table_contains(pad_to_padDescriptionMap, pad) )
            {
               ordered_g_hash_table_insert(pad_to_padDescriptionMap, pad, desc);
            }
            else
            {
               g_free(desc);
            }
         }
         pad_list = pad_list->next;
      }
   }

   return TRUE;
}



static gboolean populate_pad_connection_info(RemoteOffloadBinSerializer *serializer,
                                             guint32 outside_bin_padstartindex,
                                             OrderedGHashTable *pad_to_padDescriptionMap,
                                             GArray *remoteconnections,
                                             GstByteWriter *pDescByteWriter)
{

   guint32 current_outsidebin_pad_id = outside_bin_padstartindex;
   gboolean ret = TRUE;

   OrderedGHashTableIter iter;
   gpointer key, value;
   ordered_g_hash_table_iter_init (&iter, pad_to_padDescriptionMap);
   while(ordered_g_hash_table_iter_next (&iter, &key, &value) && ret)
   {
      GstPad *pad = (GstPad *)key;
      PadDescription *desc = (PadDescription *)value;

      //get the connected pad (the pad peer)
      GstPad *peerpad = gst_pad_get_peer(pad);

      //attempt to find it
      if( !ordered_g_hash_table_contains(pad_to_padDescriptionMap, peerpad) )
      {
         //the connected pad does not reside inside this bin, so we want to find the peer pad that
         // resides outside of the bin. That pad will get linked to some remoteoffload src/sink
         // element.
         desc->connected_pad_id = current_outsidebin_pad_id++;

         //This is a pretty roundabout way to get the "real" peer pad that resides outside of the
         // bin.
         // There's probably a simpler way to do this..

         //peerpad should be a proxy (apparently..)
         if( !GST_IS_PROXY_PAD(peerpad) )
         {
            GST_ERROR_OBJECT (serializer, "peerpad should be a proxy pad");
            return FALSE;
         }

         GstProxyPad *internalproxy = gst_proxy_pad_get_internal((GstProxyPad *)peerpad);

         //that should be a ghost pad
         if( !GST_IS_GHOST_PAD(internalproxy) )
         {
            GST_ERROR_OBJECT (serializer, "internalproxy should be a ghost pad");
            return FALSE;
         }
         GstGhostPad *ghostpad = GST_GHOST_PAD(internalproxy);

         //finally, get the peer of the ghostpad. This is the "real" peer pad that resides outside
         // of this bin
         GstPad *peerghostpad = gst_pad_get_peer((GstPad *)ghostpad);

         RemoteElementConnectionCandidate candidate;
         candidate.id = desc->connected_pad_id;
         candidate.pad = peerghostpad;

         g_array_append_val(remoteconnections, candidate);

         gst_object_unref(peerghostpad);
         gst_object_unref(internalproxy);
      }
      else
      {
         PadDescription *finditdesc =
               ordered_g_hash_table_lookup(pad_to_padDescriptionMap, peerpad);
         if( G_LIKELY(finditdesc) )
         {
            desc->connected_pad_id = finditdesc->pad_id;
         }
         else
         {
            GST_ERROR_OBJECT (serializer, "Could not map peerpad to a PadDescription");
            ret = FALSE;
         }
      }

      //write the PadDescription to the bytestream
      gst_byte_writer_put_data (pDescByteWriter, (const guint8 *) desc, sizeof (PadDescription));

      gst_object_unref(peerpad);
   }

   return ret;
}


//TODO: These "*_to_mem" functions are useful enough to be exposed as some common "utility" func's.
static GstMemory* bytewriter_to_mem(GstByteWriter *pByteWriter)
{
   GstMemory *mem = NULL;
   gsize bw_size = gst_byte_writer_get_pos(pByteWriter);

   if( bw_size )
   {
      void *user_data =  gst_byte_writer_reset_and_get_data(pByteWriter);
      mem= gst_memory_new_wrapped((GstMemoryFlags)0,
                                  user_data,
                                  bw_size,
                                  0,
                                  bw_size,
                                  user_data,
                                  GFreeMemDestroy);
   }

   return mem;
}

static GstMemory* virt_to_gstmemory(void *pVirt, gsize size)
{
   GstMemory *mem = NULL;

   if( pVirt && (size>0) )
   {
      mem= gst_memory_new_wrapped((GstMemoryFlags)0,
                                  pVirt,
                                  size,
                                  0,
                                  size,
                                  pVirt,
                                  GFreeMemDestroy);
   }

   return mem;
}




gboolean remote_offload_serialize_bin(RemoteOffloadBinSerializer *serializer,
                                      GstBin *pBin,
                                      GArray **memBlockArrayUser,
                                      GArray **remoteconnectionsoutput)
{
   if( !REMOTEOFFLOAD_IS_BINSERIALIZER(serializer) || !pBin ||
         !memBlockArrayUser || !remoteconnectionsoutput )
      return FALSE;

   BinDescriptionHeader *pheader = g_malloc(sizeof(BinDescriptionHeader));

   gchar *binname = GST_ELEMENT_NAME((GstElement *)pBin);
   if( G_UNLIKELY(g_snprintf(pheader->binname,
                  sizeof(pheader->binname),
                  "%s", binname) > sizeof(pheader->binname)) )
   {
      GST_ERROR_OBJECT (serializer,
                        "bin name %s is too long to fit BinDescriptionHeader.binname "
                        "(%"G_GSIZE_FORMAT" bytes).. skipping",
                        binname, sizeof(pheader->binname));
      g_free(pheader);
      return FALSE;
   }

   gboolean ret = TRUE;

   //the bytewriter for serialized bin, element, property, & pad description structures
   GstByteWriter descbw;
   gst_byte_writer_init (&descbw);

   //if an individual GstMemory block returned by an element serializer
   // is small enough (<= to SMALL_PROP_THRESHOLD), it is merged into
   // this 'smallpropbw', rather than being sent separately. We take
   // the hit of an extra copy, but probably reduce I/O overhead
   // by not sending lots of really small blocks separately.
   GstByteWriter smallpropbw;
   gst_byte_writer_init (&smallpropbw);

   //initialize this with 4 bytes of "dummy" data to start with, only to
   // avoid the corner case where this never gets written to during the
   // bin serialization process.
   {
      guint dummy = 0;
      gst_byte_writer_put_data (&smallpropbw, (const guint8 *) &dummy, sizeof (dummy));
   }

   *memBlockArrayUser = NULL;
   GArray *memBlockArray = g_array_new(FALSE, FALSE, sizeof(GstMemory *));

   //we will have at least 'MEMARRAY_INDEX_DEFAULT_MIN' entries in the memBlockArray.
   //1. The block representing the data written to descbw
   //2. The block representing the data written to smallpropbw
   //So, we prep the memBlockArray by adding two NULL elements
   // to start with. At the end we'll set them to the valid
   // GstMemory objects.
   {
      GstMemory *placeholder = NULL;
      for( guint i = 0; i < MEMARRAY_INDEX_DEFAULT_MIN; i++ )
      {
        g_array_append_val(memBlockArray, placeholder);
      }
   }

   //we will push every GstElement contained within the GstBin into this array
   GArray *elemArray = g_array_new(FALSE, FALSE, sizeof(GstElement *));

   //An array of RemoteElementConnectionCandidate's, which we will fill and pass
   // back to the user
   GArray *remoteconnections = g_array_new(FALSE, FALSE, sizeof(RemoteElementConnectionCandidate));

   //a (GstElement *)-to-(guint32 id) map.
   GHashTable *elem_to_id_map = g_hash_table_new(g_direct_hash, g_direct_equal);

   //a (GstPad *)-to-(PadDescription *) map.
   OrderedGHashTable *pad_to_padDescriptionMap =
         ordered_g_hash_table_new(g_direct_hash, g_direct_equal, NULL, PadDescDestroyNotify);

   pheader->nelements = 0;
   pheader->npads = 0;

   if( populate_elem_array(pBin, elemArray) )
   {
      pheader->nelements = elemArray->len;

      if( serialize_elements(serializer,
                             elemArray,
                             &descbw,
                             &smallpropbw,
                             memBlockArray,
                             elem_to_id_map) )
      {
         if( populate_pad_to_paddesc_map(serializer,
                                         elemArray,
                                         elem_to_id_map,
                                         pad_to_padDescriptionMap) )
         {
            pheader->npads = ordered_g_hash_table_size(pad_to_padDescriptionMap);

            if( !populate_pad_connection_info(serializer,
                                              pheader->npads + PAD_ID_START_INDEX,
                                              pad_to_padDescriptionMap,
                                              remoteconnections,
                                              &descbw)  )
            {
               GST_ERROR_OBJECT (serializer, "populate_pad_connection_info() failed");
               ret = FALSE;
            }
         }
         else
         {
            GST_ERROR_OBJECT (serializer, "populate_pad_to_paddesc_map() failed");
            ret = FALSE;
         }
      }
      else
      {
         GST_ERROR_OBJECT (serializer, "serialize_elements() failed");
         ret = FALSE;
      }

   }
   else
   {
      GST_ERROR_OBJECT (serializer, "populate_elem_array() failed");
      ret = FALSE;
   }

   g_object_unref(pad_to_padDescriptionMap);
   g_hash_table_destroy(elem_to_id_map);
   g_array_free(elemArray, TRUE);

   GstMemory *memdesc = bytewriter_to_mem(&descbw);
   if( !memdesc )
   {
      GST_ERROR_OBJECT (serializer, "Error converting desc bytewriter to GstMemory");
      ret = FALSE;
   }

   GstMemory *memsmallprop = bytewriter_to_mem(&smallpropbw);
   if( !memsmallprop )
   {
      GST_ERROR_OBJECT (serializer, "Error converting smallprop bytewriter to GstMemory");
      ret = FALSE;
   }


   GstMemory *memheader = virt_to_gstmemory(pheader, sizeof(BinDescriptionHeader));
   if( !memheader )
   {
      GST_ERROR_OBJECT (serializer, "Error converting header to GstMemory");
      ret = FALSE;
   }

   g_array_index(memBlockArray, GstMemory *, MEMARRAY_INDEX_HEADER) = memheader;
   g_array_index(memBlockArray, GstMemory *, MEMARRAY_INDEX_DESC) = memdesc;
   g_array_index(memBlockArray, GstMemory *, MEMARRAY_INDEX_SMALL_PROP) = memsmallprop;

   //if some error happened, perform some cleanup on objects
   // that we would have passed back to the caller.
   if( !ret )
   {
      for( guint i = 0; i < memBlockArray->len; i++ )
      {
         GstMemory *mem = g_array_index(memBlockArray, GstMemory *, i);
         if( mem )
         {
            gst_memory_unref(mem);
         }
      }
      g_array_free(memBlockArray, TRUE);
      memBlockArray = 0;

      g_array_free(remoteconnections, TRUE);
      remoteconnections = 0;
   }

   *remoteconnectionsoutput = remoteconnections;
   *memBlockArrayUser = memBlockArray;

   return ret;
}

static gboolean deserialize_elements(RemoteOffloadBinSerializer *serializer,
                                     const BinDescriptionHeader *header,
                                     guint8 **pDescriptionBase,
                                     GArray *memBlockArray,
                                     GstBin *pBin,
                                     GHashTable *idToElementMap)
{
   guint8 *pDescription = *pDescriptionBase;
   GstMemory **gstmemarray = (GstMemory **)memBlockArray->data;

   //for each element
   for( guint32 i = 0; i < header->nelements; i++)
   {
      ElementDescriptionHeader *pHeader = (ElementDescriptionHeader *)pDescription;
      pDescription += sizeof(ElementDescriptionHeader);

      guint nblocks = *((guint*)pDescription);
      pDescription += sizeof(guint);

      ElementBlockLocation *pBlockLocations = (ElementBlockLocation *)pDescription;
      pDescription += nblocks * sizeof(ElementBlockLocation);

      GArray *elemMemBlockArray = g_array_sized_new(FALSE, FALSE, sizeof(GstMemory *), nblocks);
      GArray *subMemArray = g_array_sized_new(FALSE, FALSE, sizeof(GstMemory *), nblocks);
      for( guint bi = 0; bi < nblocks; bi++ )
      {
         if( pBlockLocations[bi].memBlockIndex >= memBlockArray->len )
         {
            GST_ERROR_OBJECT (serializer,
                              "pBlockLocations[%u].memBlockIndex >= memBlockArray->len (%u)",
                              bi, memBlockArray->len);
            g_array_free(elemMemBlockArray, TRUE);
            g_array_free(subMemArray, TRUE);
            return FALSE;
         }

         GstMemory *mem = gstmemarray[pBlockLocations[bi].memBlockIndex];
         gsize memoffset;
         gsize memmaxsize;
         gsize memcurrentsize = gst_memory_get_sizes(mem, &memoffset, &memmaxsize);

         if( (pBlockLocations[bi].memBlockOffset + pBlockLocations[bi].size) > memcurrentsize )
         {
            GST_ERROR_OBJECT (serializer,
                              "pBlockLocations[%u].memBlockOffset=(%u) + "
                              "pBlockLocations[%u].size=(%u)) > memcurrentsize=(%"G_GSIZE_FORMAT")",
                              bi, pBlockLocations[bi].memBlockOffset, bi,
                              pBlockLocations[bi].size, memcurrentsize);
            g_array_free(elemMemBlockArray, TRUE);
            g_array_free(subMemArray, TRUE);
            return FALSE;
         }

         //if the location is exactly equal to one of our GstMemory blocks
         if( (pBlockLocations[bi].memBlockOffset == 0) &&
             (pBlockLocations[bi].size == memcurrentsize) )
         {
            //just append native mem to the elemMemBlockArray
            g_array_append_val(elemMemBlockArray, mem);
         }
         else
         {
            GstMemory *submem = gst_memory_copy(mem,
                                                pBlockLocations[bi].memBlockOffset,
                                                pBlockLocations[bi].size);

            g_array_append_val(elemMemBlockArray, submem);
            g_array_append_val(subMemArray, submem);
         }
      }

      GstElement *element = remote_offload_deserialize_element(serializer->element_serializer,
                                                               pHeader,
                                                               elemMemBlockArray);

      for( guint i = 0; i < subMemArray->len; i++ )
      {
         gst_memory_unref(g_array_index(subMemArray, GstMemory *, i));
      }

      g_array_free(elemMemBlockArray, TRUE);
      g_array_free(subMemArray, TRUE);

      if( !element )
      {
         GST_ERROR_OBJECT (serializer, "Error deserializing  %s (%s)",
                           pHeader->factoryname, pHeader->elementname);
         return FALSE;
      }

      if( !gst_bin_add(pBin, element) )
      {
         GST_ERROR_OBJECT (serializer, "Error adding element %p %s (%s) to bin",
                           element, pHeader->factoryname, pHeader->elementname);
         return FALSE;
      }

      g_hash_table_insert(idToElementMap, GINT_TO_POINTER(pHeader->id), element);
   }

   *pDescriptionBase = pDescription;

   return TRUE;
}


static gboolean link_pads(RemoteOffloadBinSerializer *serializer,
                          const BinDescriptionHeader *header,
                          guint8 *pDescription,
                          GHashTable *idToElementMap,
                          GArray *remoteconnections)
{
   PadDescription *pPadDescription = (PadDescription *)pDescription;

   OrderedGHashTable *padIdToPadDescription =
         ordered_g_hash_table_new(g_direct_hash, g_direct_equal,
                                  NULL, NULL);

   //for each pad
   for( guint32 i = 0; i < header->npads; i++ )
   {
      ordered_g_hash_table_insert(padIdToPadDescription,
                                  GINT_TO_POINTER(pPadDescription[i].pad_id), &pPadDescription[i]);
   }

   //now, link the pads
   {
      OrderedGHashTableIter iter;
      gpointer key, value;
      ordered_g_hash_table_iter_init (&iter, padIdToPadDescription);
      while(ordered_g_hash_table_iter_next (&iter, &key, &value))
      {
         PadDescription *pPadDescription = (PadDescription *)value;

         //we should definitely have a GstElement for this pad
         GstElement *thisgstelement =
               g_hash_table_lookup(idToElementMap,GINT_TO_POINTER(pPadDescription->element_id));
         if( !thisgstelement )
         {
            GST_ERROR_OBJECT (serializer, "Element id %d not found in the id-to-element map",
                              pPadDescription->element_id);
            g_object_unref(padIdToPadDescription);
            return FALSE;
         }

         //find the linked element pad
         PadDescription *linkedpaddesc =
               ordered_g_hash_table_lookup(padIdToPadDescription,
                                           GINT_TO_POINTER(pPadDescription->connected_pad_id));

         //if the id is not found, this is a candidate pad to be linked to a
         // ingress / egress transfer element
         if( !linkedpaddesc )
         {
            GstPad *pad;
            switch(pPadDescription->pad_presence)
            {
               case GST_PAD_REQUEST:
                  pad = gst_element_get_request_pad(thisgstelement, pPadDescription->padname);
               break;

               case GST_PAD_SOMETIMES:
                  GST_WARNING_OBJECT(serializer,
                                     "pad (%s) from element (%s) is a described as a sometimes pad "
                                     "(which is not expected)...",
                                     pPadDescription->padname, GST_ELEMENT_NAME(thisgstelement));
               case GST_PAD_ALWAYS:
                  pad = gst_element_get_static_pad(thisgstelement, pPadDescription->padname);
               break;

               default:
                  GST_ERROR_OBJECT (serializer, "Unknown GstPadPresence type\n");
                  g_object_unref(padIdToPadDescription);
                  return FALSE;
               break;
            }

            if( !pad )
            {
               GST_ERROR_OBJECT (serializer, "Error! Unable to get pad (%s) from element (%s)",
                                 pPadDescription->padname, GST_ELEMENT_NAME(thisgstelement));
               g_object_unref(padIdToPadDescription);
               return FALSE;
            }

            RemoteElementConnectionCandidate candidate;
            candidate.id = pPadDescription->connected_pad_id;
            candidate.pad = pad;

            g_array_append_val(remoteconnections, candidate);

            g_object_unref(pad);
         }
         else
         {
            //the pad that we are connecting to is inside this bin, but we only want to link
            // from src to sink.
            if( pPadDescription->pad_type == BINDESC_PADTYPE_SRC )
            {
               GstElement *connectedgstelement =
                     g_hash_table_lookup(idToElementMap,
                                         GINT_TO_POINTER(linkedpaddesc->element_id));


               if( !connectedgstelement )
               {
                  GST_ERROR_OBJECT (serializer,
                                    "Element id %d not found in the id-to-element map\n",
                                    linkedpaddesc->element_id);
                  g_object_unref(padIdToPadDescription);
                  return FALSE;
               }

               GstElement *srcElement = thisgstelement;
               GstElement *sinkElement = connectedgstelement;

               if( !gst_element_link_pads(srcElement, pPadDescription->padname,
                                          sinkElement, linkedpaddesc->padname) )
               {
                  GST_ERROR_OBJECT (serializer, "Error in gst_element_link_pads(%p, %s, %p, %s)",
                                    srcElement, pPadDescription->padname,
                                    sinkElement, linkedpaddesc->padname);
                  g_object_unref(padIdToPadDescription);
                  return FALSE;
               }
            }
         }
      }
   }

   g_object_unref(padIdToPadDescription);

   return TRUE;
}

//Given a bin, serialize it.
// header: input
// pDescription: binary description
// remoteconnections: an array of RemoteElementConnectionCandidate's
GstBin* remote_offload_deserialize_bin(RemoteOffloadBinSerializer *serializer,
                                       GArray *memBlockArray,
                                       GArray **remoteconnectionsoutput)
{
   if( !REMOTEOFFLOAD_IS_BINSERIALIZER(serializer) ||
         !memBlockArray || !remoteconnectionsoutput )
      return NULL;

   if( memBlockArray->len < MEMARRAY_INDEX_DEFAULT_MIN )
   {
      GST_ERROR_OBJECT (serializer,
                        "memBlockArray->len < MEMARRAY_INDEX_DEFAULT_MIN(%d)",
                        MEMARRAY_INDEX_DEFAULT_MIN);
      return NULL;
   }

   GstMemory **gstmemarray = (GstMemory **)memBlockArray->data;


   //map the header
   GstMapInfo headerMap;
   if( gst_memory_map (gstmemarray[MEMARRAY_INDEX_HEADER], &headerMap, GST_MAP_READ) )
   {
      if( headerMap.size != sizeof(BinDescriptionHeader))
      {
         GST_ERROR_OBJECT (serializer, "gstmemarray[MEMARRAY_INDEX_HEADER] is wrong size\n");
         gst_memory_unmap (gstmemarray[MEMARRAY_INDEX_HEADER], &headerMap);
         return NULL;
      }

      BinDescriptionHeader *pHeader = (BinDescriptionHeader *)headerMap.data;

      gboolean state_okay = TRUE;

      GHashTable *idToElementMap = g_hash_table_new(g_direct_hash, g_direct_equal);
      GArray *remoteconnections =
            g_array_new(FALSE, FALSE, sizeof(RemoteElementConnectionCandidate));
      GstBin* pBin = GST_BIN(gst_bin_new (pHeader->binname));

      GstMapInfo descMap;
      if( gst_memory_map (gstmemarray[MEMARRAY_INDEX_DESC], &descMap, GST_MAP_READ) )
      {
         guint8 *pDescription = (guint8 *)descMap.data;

         if( deserialize_elements(serializer,
                                   pHeader,
                                   &pDescription,
                                   memBlockArray,
                                   pBin,
                                   idToElementMap) )
         {
            if( !link_pads(serializer,
                          pHeader,
                          pDescription,
                          idToElementMap,
                          remoteconnections) )
            {
               GST_ERROR_OBJECT (serializer, "Error in link_pads");
               state_okay = FALSE;
            }
         }
         else
         {
            GST_ERROR_OBJECT (serializer, "Error in deserialize_elements");
            state_okay = FALSE;
         }

         gst_memory_unmap (gstmemarray[MEMARRAY_INDEX_DESC], &descMap);
      }
      else
      {
         GST_ERROR_OBJECT (serializer, "Error mapping description block for reading");
         state_okay = FALSE;
      }

      g_hash_table_destroy(idToElementMap);

      if( !state_okay )
      {
         gst_object_unref (GST_OBJECT (pBin));
         pBin = NULL;

         g_array_free(remoteconnections, TRUE);
         remoteconnections = NULL;
      }

      *remoteconnectionsoutput = remoteconnections;

      gst_memory_unmap (gstmemarray[MEMARRAY_INDEX_HEADER], &headerMap);

      return pBin;
   }
   else
   {
      GST_ERROR_OBJECT (serializer, "Error mapping header GstMemory for reading");
      return NULL;
   }

}
