/* Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#ifndef BASIC_OSTREAM_INCLUDED
#define BASIC_OSTREAM_INCLUDED
#include "my_sys.h"
#include "sql_string.h"

/**
   The abstract class for basic output streams which provides write
   operation.
*/
class Basic_ostream {
 public:
  /**
     Write some bytes into the output stream.
     When all data is written into the stream successfully, then it return
     false. Otherwise, true is returned. It will never returns false when
     partial data is written into the stream.

     @param[in] buffer  Data to be written
     @param[in] length  Length of the data
     @retval false  Success.
     @retval true  Error.
  */
  virtual bool write(const unsigned char *buffer, my_off_t length) = 0;
  virtual ~Basic_ostream() {}
};

/**
   Truncatable_ostream abstract class provides seek() and truncate() interfaces
   to all truncatable output streams.
*/
class Truncatable_ostream : public Basic_ostream {
 public:
  /**
     Truncate some data at the end of the output stream.

     @param[in] offset  Where the output stream will be truncated to.
     @retval false  Success
     @retval true  Error
  */
  virtual bool truncate(my_off_t offset) = 0;
  /**
     Put the write position to a given offset. The offset counts from the
     beginning of the file.

     @param[in] offset  Where the write position will be
     @retval false  Success
     @retval true  Error
  */
  virtual bool seek(my_off_t offset) = 0;
};

/**
   An output stream based on IO_CACHE class.
*/
class IO_CACHE_ostream : public Truncatable_ostream {
 public:
  IO_CACHE_ostream();
  IO_CACHE_ostream &operator=(const IO_CACHE_ostream &) = delete;
  IO_CACHE_ostream(const IO_CACHE_ostream &) = delete;
  ~IO_CACHE_ostream() override;

  /**
     Open the output stream. It opens related file and initialize IO_CACHE.

     @param[in] log_file_key  The PSI_file_key for this stream
     @param[in] file_name  The file will be opened
     @param[in] flags  The flags used by IO_CACHE.
     @retval false  Success
     @retval true  Error
  */
  bool open(
#ifdef HAVE_PSI_INTERFACE
      PSI_file_key log_file_key,
#endif
      const char *file_name, myf flags);
  /**
     Closes the stream. It deinitializes IO_CACHE and close the file
     it opened.

     @retval false  Success
     @retval true  Error
  */
  bool close();

  bool write(const unsigned char *buffer, my_off_t length) override;
  bool seek(my_off_t offset) override;
  bool truncate(my_off_t offset) override;

  /**
     Flush data to IO_CACHE's file if there is any data in IO_CACHE's buffer.

     @retval false  Success
     @retval true  Error
  */
  bool flush();

  /**
     Syncs the file to disk. It doesn't check and flush any remaining data still
     left in IO_CACHE's bufffer. So a call to flush() is necessary in order to
     persist all data including the data in buffer.

     @retval false  Success
     @retval true  Error
  */
  bool sync();

 private:
  IO_CACHE m_io_cache;
};

/**
   A basic output stream based on StringBuffer class. It has a stack buffer of
   size BUFFER_SIZE. It will allocate memory to create a heap buffer if
   data exceeds the size of heap buffer.
 */
template <int BUFFER_SIZE>
class StringBuffer_ostream : public Basic_ostream,
                             public StringBuffer<BUFFER_SIZE> {
 public:
  StringBuffer_ostream() {}
  StringBuffer_ostream(const StringBuffer_ostream &) = delete;
  StringBuffer_ostream &operator=(const StringBuffer_ostream &) = delete;

  bool write(const unsigned char *buffer, my_off_t length) override {
    return StringBuffer<BUFFER_SIZE>::append(
        reinterpret_cast<const char *>(buffer), length);
  }
};

#endif  // BASIC_OSTREAM_INCLUDED
