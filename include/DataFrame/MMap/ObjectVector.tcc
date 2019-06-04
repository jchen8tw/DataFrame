// Hossein Moein
// Juine 4, 2019
// Copyright (C) 2019-2022 Hossein Moein
// Distributed under the BSD Software License (see file License)


#include <DataFrame/FixedSizeString.h>
#include <DataFrame/MMap/ObjectVector.h>

#include <exception>
#include <sys/types.h>
#include <sys/mman.h>

#ifndef _WIN32

//
// There must be a nice concise language inside C++ trying to get out
//

// ----------------------------------------------------------------------------

namespace hmdf
{

template<typename D, typename B>
ObjectVector<D, B>::
ObjectVector(const char *name, ACCESS_MODE access_mode, size_type buffer_size)
    : BaseClass (name, BaseClass::_bappend_, buffer_size)  {

    const bool  just_created = BaseClass::get_file_size () == 0;

    if (just_created)  {
       // Create the header record
       //
        if (! BaseClass::write (&header, HEADER_SIZE, 1))
            throw std::runtime_error ("ObjectVector::ObjectVector<<(): "
                                      "Cannot write(). header record");

        const _internal_header_type meta_data (0, time (nullptr));

       // Create the meta data record
       //
        if (! BaseClass::write (&meta_data, _INTERNAL_HEADER_SIZE, 1))
            throw std::runtime_error ("ObjectVector::ObjectVector<<(): Cannot"
                                      " write(). internal header record");

        flush ();
    }
    else  {
        if (BaseClass::get_file_size () < _DATA_START_POINT)  {
            String1K    err;

            err.printf ("ObjectVector::ObjectVector(): "
                        "ObjectVector seems to be in an inconsistent "
                        "state (%d).",
                        BaseClass::get_file_size ());
            throw std::runtime_error (err.c_str ());
        }
    }

   // Extract the meta data record
   //
    _internal_header_type   *mdata_ptr =
        reinterpret_cast<_internal_header_type *>
            (reinterpret_cast<char *>(BaseClass::_get_base_ptr ()) +
             HEADER_SIZE);

    cached_object_count_ = mdata_ptr->object_count;
    seek (cached_object_count_);
}

// ----------------------------------------------------------------------------

template<typename D, typename B>
typename ObjectVector<D, B>::size_type
ObjectVector<D, B>::tell () const noexcept  {

    const size_type pos = BaseClass::tell ();

    return ((pos - _DATA_START_POINT) / DATA_SIZE);
}

// ----------------------------------------------------------------------------

template<typename D, typename B>
ObjectVector<D, B>::~ObjectVector ()  {

    if (BaseClass::get_device_type () == BaseClass::_shared_memory_ ||
        BaseClass::get_device_type () == BaseClass::_mmap_file_)
        flush ();
}

// ----------------------------------------------------------------------------

template<typename D, typename B>
inline typename ObjectVector<D, B>::data_type &
ObjectVector<D, B>::operator [] (size_type index)  {

    data_type   &this_item =
        *(reinterpret_cast<data_type *>
              (reinterpret_cast<char *>(BaseClass::_get_base_ptr ()) +
               _DATA_START_POINT) +
          index);

    return (this_item);
}

// ----------------------------------------------------------------------------

template<typename D, typename B>
inline const typename ObjectVector<D, B>::data_type &
ObjectVector<D, B>::operator [] (size_type index) const noexcept  {

    const data_type &this_item =
        *(reinterpret_cast<const data_type *>
              (reinterpret_cast<const char *>(BaseClass::_get_base_ptr ()) +
               _DATA_START_POINT) +
          index);

    return (this_item);
}

// ----------------------------------------------------------------------------

template<typename D, typename B>
time_t ObjectVector<D, B>::creation_time () const noexcept  {

    const _internal_header_type &meta_data =
        *(reinterpret_cast<const _internal_header_type *>
              (reinterpret_cast<const char *>(BaseClass::_get_base_ptr ()) +
               HEADER_SIZE));

    return (meta_data.creation_time);
}

// ----------------------------------------------------------------------------

template<typename D, typename B>
void ObjectVector<D, B>::set_access_mode (ACCESS_MODE am) const  {

    if (empty ())
        return;

    const   int rc =
        ::posix_madvise (BaseClass::_get_base_ptr (),
                         BaseClass::get_mmap_size (),
                         (am == _normal_) ? POSIX_MADV_NORMAL
                             : (am == _need_now_) ? POSIX_MADV_WILLNEED
                             : (am == _random_) ? POSIX_MADV_RANDOM
                             : (am == _sequential_) ? POSIX_MADV_SEQUENTIAL
                             : (am == _dont_need_) ? POSIX_MADV_DONTNEED
                             : -1);

    if (rc)  {
        String1K    err;

        err.printf ("ObjectVector::set_access_mode(): ::posix_madvise(): "
                    "(%d) %s",
                    errno, strerror (errno));
        throw std::runtime_error (err.c_str ());
    }

    return;
}

// ----------------------------------------------------------------------------

template<typename D, typename B>
bool ObjectVector<D, B>::seek_ (size_type obj_num) const noexcept  {

    BaseClass   *nc_ptr =
        const_cast<BaseClass *>(static_cast<const BaseClass *>(this));

    return (nc_ptr->seek (_DATA_START_POINT + (obj_num * DATA_SIZE),
                          BaseClass::_seek_set_) == 0 ? true : false);
}

// ----------------------------------------------------------------------------

template<typename D, typename B>
int ObjectVector<D, B>::write_ (const data_type *data_ele, size_type count)  {

    const   int rc = BaseClass::write (data_ele, DATA_SIZE, count);

    if (rc != count)  {
        String1K    err;

        err.printf ("ObjectVector::write(): Cannot write %llu elements. "
                    "Instead wrote %d elements.",
                    count, rc);
        throw std::runtime_error (err.c_str ());
    }

    _internal_header_type   &meta_data =
        *(reinterpret_cast<_internal_header_type *>
              (reinterpret_cast<char *>(BaseClass::_get_base_ptr ()) +
               HEADER_SIZE));

    meta_data.object_count += count;
    cached_object_count_ += count;
    return (rc);
}

// ----------------------------------------------------------------------------

template<typename D, typename B>
typename ObjectVector<D, B>::iterator
ObjectVector<D, B>::erase (iterator first, iterator last)  {

    const size_type lnum = &(*last) - &(*begin ());

    ::memmove (&(*first), &(*last),
               (&(*end ()) - &(*last)) * sizeof (data_type));

    const size_type s = &(*last) - &(*first);

    BaseClass::truncate (BaseClass::_file_size - s * sizeof (data_type));

    _internal_header_type   &meta_data =
        *(reinterpret_cast<_internal_header_type *>
              (reinterpret_cast<char *>(BaseClass::_get_base_ptr ()) +
               HEADER_SIZE));

    meta_data.object_count -= s;
    cached_object_count_ -= s;
    seek (cached_object_count_);

    return (iterator_at (lnum - s));
}

// ----------------------------------------------------------------------------

template<typename D, typename B>
template<typename I>
void ObjectVector<D, B>::insert (iterator pos, I first, I last)  {

    const long      int to_add = &(*last) - &(*first);
    const size_type pos_index = &(*pos) - &(*begin ());

    BaseClass::truncate (BaseClass::_file_size + to_add * sizeof (data_type));

    const iterator  new_pos = iterator_at (pos_index);

    ::memmove (&(*(new_pos + to_add)), &(*new_pos),
               (&(*end ()) - &(*new_pos)) * sizeof (data_type));
    ::memcpy (&(*new_pos), &(*first), to_add * sizeof (data_type));

    _internal_header_type   &meta_data =
        *(reinterpret_cast<_internal_header_type *>
              (reinterpret_cast<char *>(BaseClass::_get_base_ptr ()) +
               HEADER_SIZE));

    meta_data.object_count += to_add;
    cached_object_count_ += to_add;
    seek (cached_object_count_);

    return;
}

} // namespace hmdf

#endif // _WIN32

// ----------------------------------------------------------------------------

// Local Variables:
// mode:C++
// tab-width:4
// c-basic-offset:4
// End:
