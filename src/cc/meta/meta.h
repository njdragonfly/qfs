/*!
 * $Id$
 *
 * \file meta.h
 * \brief Base class and derived classes for KFS metadata objects.
 * \author Blake Lewis (Kosmix Corp.)
 *
 * Copyright 2008-2012 Quantcast Corp.
 * Copyright 2006-2008 Kosmix Corp.
 *
 * This file is part of Kosmos File System (KFS).
 *
 * Licensed under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
#if !defined(KFS_META_H)
#define KFS_META_H

#include "Key.h"
#include "MetaNode.h"
#include "common/config.h"
#include "common/time.h"
#include "common/hsieh_hash.h"
#include "common/kfsdecls.h"

#include <ostream>
#include <string>
#include <cassert>

namespace KFS {

using std::string;
using std::ostream;

/*!
 * \brief fixed-length unique id generator
 *
 * Unique fixed-length id generator for each file and directory and chunk
 * in the system.
 */

class UniqueID {
    seqid_t n;      //!< id of this object
    seqid_t seed;       //!< seed for generator
public:
    /*!
     * \brief generate a new id
     */
    fid_t genid() { return ++seed; }
    fid_t getseed() { return seed; }
    void setseed(seqid_t s) { seed = s; }
    UniqueID(seqid_t id, seqid_t s): n(id), seed(s) { }
    UniqueID(): n(0), seed(0) { }
    seqid_t id() const { return n; }    //!< return id
};

/*!
 * \brief base class for data objects (leaf nodes)
 */
class Meta: public MetaNode {
protected:
    virtual ~Meta() { }
public:
    Meta(MetaType t): MetaNode(t) { }
    bool skip() const { return testflag(META_SKIP); }
    void markskip() { setflag(META_SKIP); }
    void clearskip() { clearflag(META_SKIP); }
    int checkpoint(ostream &file) const
    {
        show(file) << '\n';
        return file.fail() ? -EIO : 0;
    }
    //!< Compare for equality
    virtual bool match(const Meta *test) const = 0;
private:
    Meta(const Meta&);
    Meta& operator=(const Meta&);
};

/*!
 * \brief downcast from base to derived metadata types
 */
template <typename T> T *
refine(Meta *m)
{
    return static_cast <T *>(m);
}

template <typename T> const T *
refine(const Meta *m)
{
    return static_cast <const T *>(m);
}

class MetaFattr;

/*!
 * \brief Directory entry, mapping a file name to a file id
 */
class MetaDentry: public Meta {
    fid_t      fid;  //!< id of this item's owner
    fid_t      dir;  //!< id of parent directory
    KeyData    hash;
    MetaFattr* fattr;
    string     name; //!< name of this entry
protected:
    MetaDentry(fid_t parent, const string& fname, fid_t myID, MetaFattr* fa)
        : Meta(KFS_DENTRY),
          fid(myID),
          dir(parent),
          hash(nameHash(fname)),
          fattr(fa),
          name(fname)
          {}

    MetaDentry(const MetaDentry *other)
        : Meta(KFS_DENTRY),
          fid(other->id()),
          dir(other->dir),
          hash(other->hash),
          fattr(other->fattr),
          name(other->name)
          {}
    virtual ~MetaDentry() {}
public:
    static inline KeyData nameHash(const string& name)
    {
        // Key(t,d1,d2) discards d2 low order bits.
        // The hash is 32 bit. Storing in MetaDentry 32 instead of 64
        // bit hash currently doesn't save anything due to alignment
        // with 64 bit compile.
        Hsieh_hash_fcn f;
        return ((KeyData)(f(name)) << 4);
    }
    static MetaDentry* create(fid_t parent, const string& fname, fid_t myID,
        MetaFattr* fa)
    {
        return new (allocate<MetaDentry>())
            MetaDentry(parent, fname, myID, fa);
    }
    static MetaDentry* create(const MetaDentry *other)
    {
        return new (allocate<MetaDentry>()) MetaDentry(other);
    }
    virtual void destroy()
    {
        this->~MetaDentry();
        deallocate(this);
    }
    fid_t id() const { return fid; }    //!< return the owner id
    virtual const Key key() const { return Key(KFS_DENTRY, dir, hash); }
    ostream& show(ostream& os) const;
    //!< accessor that returns the name of this Dentry
    const string& getName() const { return name; }
    fid_t getDir() const { return dir; }
    KeyData getHash() const { return hash; }
    const int compareName(const string& test) const {
        return name.compare(test);
    }
    int checkpoint(ostream &file) const;
    virtual bool match(const Meta *test) const;
    MetaFattr* getFattr() const { return fattr; }
    void setFattr(MetaFattr* fa) { fattr = fa; }
};

class BaseFattr {
protected:
    fid_t fid;      //!< id of this item's owner
public:
    BaseFattr(
        FileType  t  = KFS_NONE,
        fid_t     id = 0,
        int16_t   n  = 0)
        : fid(id),
          type(t),
          striperType(KFS_STRIPED_FILE_TYPE_NONE),
          numReplicas(n),
          numRecoveryStripes(0),
          numStripes(0),
          stripeSize(0),
          subcount1(0),
          subcount2(0),
          filesize(0)
        {}
    BaseFattr(
        FileType  t,
        fid_t     id,
        int64_t   mt,
        int64_t   ct,
        int64_t   crt,
        int64_t   c,
        int16_t   n)
        : fid(id),
          type(t),
          striperType(KFS_STRIPED_FILE_TYPE_NONE),
          numReplicas(n),
          numRecoveryStripes(0),
          numStripes(0),
          stripeSize(0),
          mtime(mt),
          ctime(ct),
          crtime(crt),
          subcount1(c),
          subcount2(0),
          filesize(0)
        {}
    FileType        type:2;         //!< file or directory
    StripedFileType striperType:5;
    int32_t         numReplicas:14; //!< Desired number of replicas for a file
    int32_t         numRecoveryStripes:7;
    int32_t         numStripes:9;
    int32_t         stripeSize:27;
    int64_t         mtime; //!< modification time
    int64_t         ctime; //!< attribute change time
    int64_t         crtime; //!< creation time
    int64_t         subcount1; //!< number of constituent chunks, or "sub" files for directory
    //!< offset in the file at which the last chunk was allocated.  For
    //!< record appends, the client will issue an allocation request asking
    //!< to append a chunk to the file.  The metaserver picks the file offset
    //!< for the chunk based on what has been allocated so far.
    // < Or sub directories count
    chunkOff_t      subcount2;
    //!< size of file: is only a hint; if we don't have the size, the client will
    //!< compute the size whenever needed.
    chunkOff_t      filesize;

    fid_t id() const { return fid; }    //!< return the owner id
    void setReplication(int16_t val) {
        numReplicas = val;
    }
    bool IsStriped() const {
        return (striperType != KFS_STRIPED_FILE_TYPE_NONE);
    }
    bool HasRecovery() const {
        return (IsStriped() && numRecoveryStripes > 0);
    }
    chunkOff_t ChunkPosToChunkBlkIndex(chunkOff_t offset) const {
        const chunkOff_t idx = offset / (chunkOff_t)CHUNKSIZE;
        return (numStripes <= 0 ? idx :
            idx / (numStripes + numRecoveryStripes));
    }
    chunkOff_t FilePosToChunkBlkIndex(chunkOff_t offset) const {
        const chunkOff_t idx = offset / (chunkOff_t)CHUNKSIZE;
        return (numStripes <= 0 ? idx : idx / numStripes);
    }
    chunkOff_t ChunkBlkSize() const {
        return ((chunkOff_t)CHUNKSIZE * (numStripes <= 0 ?
            1 : numStripes + numRecoveryStripes));
    }
    chunkOff_t ChunkPosToChunkBlkStartPos(chunkOff_t offset) const {
        return (ChunkPosToChunkBlkIndex(offset) * ChunkBlkSize());
    }
    chunkOff_t ChunkPosToChunkBlkFileStartPos(chunkOff_t offset) const {
        return (ChunkPosToChunkBlkIndex(offset) *
            (chunkOff_t)CHUNKSIZE * numStripes);
    }
    bool SetStriped(int32_t t, int32_t n, int32_t nr, int32_t ss) {
        switch ((int)t) {
            case KFS_STRIPED_FILE_TYPE_NONE:
                striperType        = KFS_STRIPED_FILE_TYPE_NONE;
                numStripes         = 0;
                numRecoveryStripes = 0;
                stripeSize         = 0;
                return true;
            case KFS_STRIPED_FILE_TYPE_RS:
                striperType = KFS_STRIPED_FILE_TYPE_RS;
            break;
            default:
                return false;
        }
        numStripes = n;
        if (numStripes <= 0 || numStripes != n) {
            return false;
        }
        numRecoveryStripes = nr;
        if (numRecoveryStripes < 0 || numRecoveryStripes != nr) {
            return false;
        }
        stripeSize = ss;
        if (stripeSize < KFS_MIN_STRIPE_SIZE ||
                ss > KFS_MAX_STRIPE_SIZE ||
                stripeSize != ss ||
                CHUNKSIZE % stripeSize != 0 ||
                stripeSize % KFS_STRIPE_ALIGNMENT != 0) {
            return false;
        }
        return true;
    }
    int64_t&          chunkcount()            { return subcount1; }
    const int64_t&    chunkcount() const      { return subcount1; }
    chunkOff_t&       nextChunkOffset()       { return subcount2; }
    const chunkOff_t& nextChunkOffset() const { return subcount2; }
    int64_t&          fileCount()             { return subcount1; }
    const int64_t&    fileCount() const       { return subcount1; }
    chunkOff_t&       dirCount()              { return subcount2; }
    const chunkOff_t& dirCount() const        { return subcount2; }
};

// Inheritance here used only to keep file attributes (fid in particular) at
// the beginning (hopefully all fits into a cache line), while avoiding extra
// method forwarding / wrappers required if permissions was declared an instance
// variable.
class MFattr : public BaseFattr, public Permissions {
public:
    MFattr(
        FileType  t  = KFS_NONE,
        fid_t     id = 0,
        int16_t   n  = 0,
        kfsUid_t  u  = kKfsUserNone,
        kfsGid_t  g  = kKfsGroupNone,
        kfsMode_t m  = 0)
        : BaseFattr(t, id, n),
          Permissions(u, g, m)
        {}
    MFattr(
        FileType  t,
        fid_t     id,
        int64_t   mt,
        int64_t   ct,
        int64_t   crt,
        int64_t   c,
        int16_t   n,
        kfsUid_t  u,
        kfsGid_t  g,
        kfsMode_t m)
        : BaseFattr(t, id, mt, ct, crt, c, n),
          Permissions(u, g, m)
        {}
};

/*!
 * \brief File or directory attributes.
 *
 * This structure plays the role of an inode in KFS.  Currently just
 * an "artist's conception"; more attritbutes will be added as needed.
 */
class MetaFattr: public Meta, public MFattr {
private:
    MetaFattr(FileType t, fid_t id, int16_t n,
        kfsUid_t u, kfsGid_t g, kfsMode_t m)
        : Meta(KFS_FATTR),
          MFattr(t, id, n, u, g, m),
          parent(0)
    {
        crtime = microseconds();
        mtime = ctime = crtime;
    }
    MetaFattr(
        FileType  t,
        fid_t     id,
        int64_t   mt,
        int64_t   ct,
        int64_t   crt,
        int64_t   c,
        int16_t   n,
        kfsUid_t  u,
        kfsGid_t  g,
        kfsMode_t m)
        : Meta(KFS_FATTR),
          MFattr(t, id, mt, ct, crt, c, n, u, g, m),
          parent(0)
        {}
protected:
    virtual ~MetaFattr() {}
public:
    MetaFattr* parent;
    static MetaFattr* create(FileType t, fid_t id, int16_t n,
        kfsUid_t u, kfsGid_t g, kfsMode_t m)
    {
        return new (allocate<MetaFattr>()) MetaFattr(t, id, n, u, g, m);
    }
    static MetaFattr* create(
        FileType  t,
        fid_t     id,
        int64_t   mt,
        int64_t   ct,
        int64_t   crt,
        int64_t   c,
        int16_t   n,
        kfsUid_t  u,
        kfsGid_t  g,
        kfsMode_t m)
    {
        return new (allocate<MetaFattr>())
            MetaFattr(t, id, mt, ct, crt, c, n, u, g, m);
    }
    virtual void destroy()
    {
        this->~MetaFattr();
        deallocate(this);
    }
    fid_t id() const { return fid; }    //!< return the owner id
    virtual const Key key() const { return Key(KFS_FATTR, id()); }
    ostream& show(ostream& os) const;
    int checkpoint(ostream &file) const;
    chunkOff_t LastChunkBlkIndex() const {
        return ChunkPosToChunkBlkIndex(nextChunkOffset() - 1);
    }
    virtual bool match(const Meta *test) const {
        return (test->metaType() == KFS_FATTR &&
            id() == refine<MetaFattr>(test)->id());
    }
};

/*!
 * \brief chunk information for a given file offset
 */
class MetaChunkInfo: public Meta {
protected:
    MetaChunkInfo(MetaFattr* fa, chunkOff_t off, chunkId_t id, seq_t v)
        : Meta(KFS_CHUNKINFO),
          fattr(fa),
          offset(off),
          chunkId(id),
          chunkVersion(v)
        {}
    virtual ~MetaChunkInfo() {}
    MetaFattr* fattr;
public:
    chunkOff_t offset;      //!< offset of chunk within file
    chunkId_t  chunkId;     //!< unique chunk identifier
    seq_t      chunkVersion;    //!< version # for this chunk
    fid_t id() const { return fattr->id(); }    //!< return the owner id
    MetaFattr* getFattr() const { return fattr; }
    virtual const Key key() const { return Key(KFS_CHUNKINFO, id(), offset); }

    void DeleteChunk();

    ostream& show(ostream& os) const;
    int checkpoint(ostream &file) const;
    virtual bool match(const Meta *test) const {
        return (test->metaType() == KFS_CHUNKINFO &&
            id() == refine<MetaChunkInfo>(test)->id());
    }
};

extern UniqueID fileID;   //!< Instance for generating unique fid
extern UniqueID chunkID;  //!< Instance for generating unique chunkId

// return a string representation of the timeval. the string is written out
// to logs/checkpoints
class ShowTime
{
public:
    ShowTime(int64_t microseconds) : t(microseconds) {}
    ostream& show(ostream& os) const {
        const int64_t kMicroseconds = 1000 * 1000;
        return (os << (t / kMicroseconds) << "/" << (t % kMicroseconds));
    }
private:
    const int64_t t;
};

inline static ostream& operator<<(ostream& os, const ShowTime& t) {
    return t.show(os);
}

}
#endif  // !defined(KFS_META_H)
