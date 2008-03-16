// Copyright (C) 2007 Codership Oy <info@codership.com>
/*
 * Interface to action protocol
 * (to be extended to support protocol versions, currently supports only v0)
 */

#ifndef _gcs_act_proto_h_
#define _gcs_act_proto_h_

#include <gcs.h> // for gcs_seqno_t

/*! Internal action fragment data representation */
typedef struct gcs_act_frag
{
    gcs_seqno_t    act_id;
    size_t         act_size;
    const void*    frag;     // shall override it only once
    size_t         frag_len;
    long           frag_no;
    gcs_act_type_t act_type;
    unsigned char  proto_ver;
}
gcs_act_frag_t;

/*! Writes header data into actual header of the message.
 *  Remainig fragment buf and length is in frag->frag and frag->frag_len */
extern long
gcs_act_proto_write (gcs_act_frag_t* frag, void* buf, size_t buf_len);

/*! Reads header data from the actual header of the message
 *  Remainig fragment buf and length is in frag->frag and frag->frag_len */
extern long
gcs_act_proto_read (gcs_act_frag_t* frag, const void* buf, size_t buf_len);

/*! Increments fragment counter when action remains the same */
extern long
gcs_act_proto_inc (void* buf);

/*! Returns protocol header size */
extern long
gcs_act_proto_hdr_size (unsigned char version);

#endif /* _gcs_act_proto_h_ */
