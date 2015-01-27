// -*- C++ -*-

#ifndef FB_ALLOC_H
#define FB_ALLOC_H

#include <assert.h>
#include <memory>
#include <new>

template <typename T, unsigned int nof_elements=100, size_t alignment=8> class fb_alloc
{
   public:

      typedef size_t    size_type;
      typedef ptrdiff_t difference_type;
      
      typedef T         value_type;
      typedef T*        pointer;
      typedef const T*  const_pointer;
      
      typedef T&        reference;
      typedef const T&  const_reference;
      
      //
      // ctors and dtors
      //      
      fb_alloc( void ) throw();
      template <typename U> fb_alloc( const fb_alloc<U,nof_elements,alignment>& ) throw();

      ~fb_alloc( void ) throw();
      
      pointer allocate( size_t n, const void* hint = 0 );
      void    deallocate( pointer p, size_t n );
      
      void    construct(  pointer p );
      void    construct(  pointer p, const T& val );
      void    destroy( pointer p );
      
      //
      // NOTE! this method only deallocate memory chunks, NOT making the calls to the object destructor
      //
      void   release( void ) throw ( const char* );

      //
      // converters
      //
      pointer       address( reference ) const;
      const_pointer address( const_reference ) const;
      
      //
      // observers
      //
      size_type    max_size( void ) const throw();
      
      unsigned int nof_elmts( void ) const;
      size_t       elsize( void ) const;
      size_t       alignmnt( void ) const;
      size_t       chunksize( void ) const;
      
      int          refcount( void ) const;
      int          nof_allocs( void ) const;

      void         dump( std::ostream& os ) const;

   protected:
    
      void clean( void );
      void grow( void ) throw (const std::bad_alloc&);
      bool check( const pointer p ) const;

      char* allocate_chunk( void ) const;
      void  deallocate_chunk( char* ptr ) const;

   private:

      struct alloc_link
      {
         struct alloc_link* next_;
      };

      unsigned int  nof_elmts_; // number of elements in one chunk
      size_t        elsize_;    // element size adjusted due to alignment restrictions
      size_t        alignment_; // alignment value
      
      char*         pool_head_;  // head of the pool of available blocks
      char*         chunk_head_; // head of the chunk list

      int*          refcount_;   // pointer to the reference counter which protects chunk list
      int*          nof_allocs_; // number of elements allocations

      static char*  global_chunk_head_;    // head of the GLOBAL chunk list
      static int    nof_allocated_chunks_; // number of allocated chunks kept in global list
      static int    nof_free_chunks_;      // number of free chunks in global list
};

template <typename T, unsigned int nof_elements, size_t alignment>  char*
fb_alloc<T,nof_elements,alignment>::global_chunk_head_ = 0;

template <typename T, unsigned int nof_elements, size_t alignment>  int
fb_alloc<T,nof_elements,alignment>::nof_allocated_chunks_ = 0;

template <typename T, unsigned int nof_elements, size_t alignment>  int
fb_alloc<T,nof_elements,alignment>::nof_free_chunks_ = 0;

template <typename T, unsigned int nof_elements, size_t alignment> 
fb_alloc<T,nof_elements,alignment>::fb_alloc( void ) throw ():

   nof_elmts_(nof_elements),
   elsize_(sizeof(T)),
   alignment_(alignment),
   pool_head_(0),
   chunk_head_(0)
{

   assert( nof_elmts_ > 0 );
   assert( elsize_ > 0 );
   assert( alignment_ > 0 );

   //
   // calculate proper element size taking into account alignment
   //
   for( ; elsize_ < elsize_ + alignment_; ++elsize_ )
   {
      if ( (elsize_ % alignment_ ) == 0 )
      {
         break;
      }
   }

   //
   // allocating refcount
   //

   // refcount_ = new (std::nothrow) int;
   refcount_ = new int;
   *refcount_ = 1;

   // nof_allocs_ = new (std::nothrow) int;
   nof_allocs_ = new int;
   *nof_allocs_ = 0;

}

//
// copy constructor, supposed to be member template
//
template <typename T, unsigned int nof_elements, size_t alignment> template <typename U> 
fb_alloc<T,nof_elements,alignment>::fb_alloc( const fb_alloc<U,nof_elements,alignment>& fba ) throw ():
   nof_elmts_( nof_elements ),
   elsize_( sizeof(T) ),
   alignment_( alignment ),
   pool_head_(0),
   chunk_head_(0)
{
   assert( nof_elmts_ > 0 );
   assert( elsize_ > 0 );
   assert( alignment_ > 0 );

   //
   // calculate proper element size taking into account alignment
   //
   for( ; elsize_ < elsize_ + alignment_; ++elsize_ )
   {
      if ( (elsize_ % alignment_ ) == 0 )
      {
         break;
      }
   }

   //
   // Share internal data iff element size is the same.
   // Actually we could share data if this->elsize_ <= fba.elsize_, 
   // or better yet if this->elsize_ <= fba.elsize_ and this->elsize_ >= fba.elsize_/2
   // TBD: Optimal sharing
   //
   if ( elsize_ == fba.elsize_ )
   {
      pool_head_  = fba.pool_head_;
      chunk_head_ = fba.chunk_head_;
      refcount_   = fba.refcount_;
      nof_allocs_ = fba.nof_allocs_;
      ++*refcount_;
      assert( *refcount_ >= 0 );
   }
   else
   {
      refcount_ = new (std::nothrow) int;
      if ( refcount_ != 0 )
      {
         *refcount_ = 1;
      }
      nof_allocs_ = new (std::nothrow) int;
      if ( nof_allocs_ != 0 )
      {
         *nof_allocs_ = 0;
      }
   }
}

template <typename T, unsigned int nof_elements, size_t alignment>
fb_alloc<T,nof_elements,alignment>::~fb_alloc( void ) throw ()
{
   if ( refcount_ != 0 )
   {
      --(*refcount_);
      assert( *refcount_ >= 0 );
      if ( *refcount_ == 0 )
      {
         clean();
         delete refcount_; refcount_ = 0;
         delete nof_allocs_; nof_allocs_ = 0;
      }
   }
}

template <typename T, unsigned int nof_elements, size_t alignment> void 
fb_alloc<T,nof_elements,alignment>::release( void ) throw (const char*)
{
   if ( refcount_ != 0 )
   {
      --(*refcount_);
      assert( *refcount_ >= 0 );
      if ( *refcount_ == 0 )
      {
         clean(); // this delete all memory blocks
         *refcount_   = 1;
         *nof_allocs_ = 0;
      }
   }
}

template <typename T, unsigned int nof_elements, size_t alignment> void 
fb_alloc<T,nof_elements,alignment>::grow( void ) throw (const std::bad_alloc&)
{
   char* p;
   
   // constructor was unable to allocate refcount or nof_allocs_, time to die
   if ( (refcount_ == 0) || ( nof_allocs_ == 0 ) )
   {
      throw std::bad_alloc();
   }
   
   char* start = allocate_chunk();

   *(char**)(start + elsize_*nof_elmts_) = *(char**)(&chunk_head_); // memcpy( start + elsize_*nof_elmts_, &chunk_head_, sizeof(char*) );
   chunk_head_ = start;
  
   //
   // set links
   //
#if 0
   char* last  = start + (nof_elmts_-1)*elsize_;

   for( p = start; p < last; p += elsize_ )
   {
      ( (alloc_link*)(p) )->next_ = (alloc_link*)( p + elsize_ );
   }

   static_cast<alloc_link*>( (void*)last )->next_ = 0;
#else
   p = start;
   int nn = ((nof_elmts_-1) + 7 )/8;
   switch ( (nof_elmts_-1) % 8)
   {
      case 0:
         do {
            ( (alloc_link*)(p) )->next_ = (alloc_link*)( p + elsize_ ); p += elsize_;
      case 7:
            ( (alloc_link*)(p) )->next_ = (alloc_link*)( p + elsize_ ); p += elsize_;
      case 6:
            ( (alloc_link*)(p) )->next_ = (alloc_link*)( p + elsize_ ); p += elsize_;
      case 5:
            ( (alloc_link*)(p) )->next_ = (alloc_link*)( p + elsize_ ); p += elsize_;
      case 4:
            ( (alloc_link*)(p) )->next_ = (alloc_link*)( p + elsize_ ); p += elsize_;
      case 3:
            ( (alloc_link*)(p) )->next_ = (alloc_link*)( p + elsize_ ); p += elsize_;
      case 2:
            ( (alloc_link*)(p) )->next_ = (alloc_link*)( p + elsize_ ); p += elsize_;
      case 1:
            ( (alloc_link*)(p) )->next_ = (alloc_link*)( p + elsize_ ); p += elsize_;
            
         } while ( --nn > 0 );
   }
   ( (alloc_link*)(p) )->next_ = 0;
#endif

   pool_head_ = start;
}

template <typename T, unsigned int nof_elements, size_t alignment> void 
fb_alloc<T,nof_elements,alignment>::clean( void )
{
   char* ptr = chunk_head_;
   while ( ptr != 0 )
   {
      *(char**)(&chunk_head_) = *(char**)(ptr + elsize_*nof_elmts_); // memcpy( &chunk_head_, ptr + elsize_*nof_elmts_, sizeof(char*) );
      deallocate_chunk( ptr );
      ptr = chunk_head_;
   }
   pool_head_  = 0;
   chunk_head_ = 0;
}

template <typename T, unsigned int nof_elements, size_t alignment> inline bool
fb_alloc<T,nof_elements,alignment>::check( const pointer p ) const
{
   char* ptr = chunk_head_;
   char* pob = static_cast<char*>( static_cast<void*>(p) );
   while ( ptr != 0 )
   {
      if ( (pob >= ptr) && (pob <= ptr + (nof_elmts_-1)*elsize_) )
      {
         return true;
      }
      *(char**)(&ptr) = *(char**)(ptr + elsize_*nof_elmts_);  // memcpy( &ptr, ptr + elsize_*nof_elmts_, sizeof(char*) );
   }
   return false;
}

template <typename T, unsigned int nof_elements, size_t alignment> inline char*
fb_alloc<T,nof_elements,alignment>::allocate_chunk( void ) const
{
#ifdef CHUNKS_RETURNED_TO_MALLOC
   return new char [ elsize_*nof_elmts_ + sizeof(char*) ];
#else
   if ( global_chunk_head_ == 0 ) // no more preallocated chunks, ask malloc nicely for additional chunk
   {
      ++nof_allocated_chunks_;
      char* pp = new char [ elsize_*nof_elmts_ + sizeof(char*) ];
      memset( pp, 0, elsize_*nof_elmts_ + sizeof(char*) );
      return pp;
   }

   //
   // chunks are available, return one from the pool
   //
   --nof_free_chunks_;
   assert( nof_free_chunks_ >= 0 );
   char* res          = global_chunk_head_;
   global_chunk_head_ = static_cast<char*>(static_cast<void*>( static_cast<alloc_link*>(static_cast<void*>(res))->next_ ));

   return res;
#endif
}

template <typename T, unsigned int nof_elements, size_t alignment> inline void
fb_alloc<T,nof_elements,alignment>::deallocate_chunk( char* p ) const
{
#ifdef CHUNKS_RETURNED_TO_MALLOC
   delete [] p;
#else
   //
   // instead of returning memory to malloc/OS,
   // keep global linked list of chunks for future requests
   //

//    std::cerr << "D: ";

   alloc_link* ptr    = static_cast<alloc_link*>( static_cast<void*>(p) );
   ptr->next_         = static_cast<alloc_link*>( static_cast<void*>(global_chunk_head_) );
   global_chunk_head_ = static_cast<char*>( static_cast<void*>(ptr) );
   ++nof_free_chunks_;

//    std::cerr << nof_free_chunks_ << std::endl;   

   assert( nof_free_chunks_ <= nof_allocated_chunks_ );

#endif
}

template <typename T, unsigned int nof_elements, size_t alignment> inline
typename fb_alloc<T,nof_elements,alignment>::pointer
fb_alloc<T,nof_elements,alignment>::allocate( size_type n, const void* hint )
{
   if ( n == 1 )
   {      
      if ( pool_head_ == 0 )
      {
         grow();
      }
      char* res  = pool_head_;
      pool_head_ = static_cast<char*>(static_cast<void*>( static_cast<alloc_link*>(static_cast<void*>(res))->next_ ));
      
      ++*nof_allocs_; 
      assert( *nof_allocs_ > 0 );

      return static_cast<pointer>(static_cast<void*>(res));
   }

   return static_cast<pointer>( ::operator new (n*sizeof(T)) );
}

template <typename T, unsigned int nof_elements, size_t alignment> inline void
fb_alloc<T,nof_elements,alignment>::deallocate( pointer p, size_type n )
{
   if ( n == 1 )
   {
      //
      // check in p is allocated by us: should walk the chunks
      //
      assert( check(p) );
      
      alloc_link* ptr = static_cast<alloc_link*>( static_cast<void*>(p) );
      ptr->next_      = static_cast<alloc_link*>( static_cast<void*>(pool_head_) );
      pool_head_      = static_cast<char*>( static_cast<void*>(ptr) );
      --*nof_allocs_;
      assert( *nof_allocs_ >= 0 );
   }
   else
   {
      ::operator delete ( p );
   }
}

template <typename T, unsigned int nof_elements, size_t alignment> inline void    
fb_alloc<T,nof_elements,alignment>::construct(  pointer p )
{
   new(p) T;
}

template <typename T, unsigned int nof_elements, size_t alignment> inline void    
fb_alloc<T,nof_elements,alignment>::construct(  pointer p, const T& val )
{
   new(p) T(val);
}

template <typename T, unsigned int nof_elements, size_t alignment> inline void
fb_alloc<T,nof_elements,alignment>::destroy( pointer p )
{
   p->~T();
}

template <typename T, unsigned int nof_elements, size_t alignment> inline
typename fb_alloc<T,nof_elements,alignment>::pointer
fb_alloc<T,nof_elements,alignment>::address( reference r ) const
{
   return &r;
}

template <typename T, unsigned int nof_elements, size_t alignment> inline
typename fb_alloc<T,nof_elements,alignment>::const_pointer 
fb_alloc<T,nof_elements,alignment>::address( const_reference cr ) const
{
   return &cr;
}

template <typename T, unsigned int nof_elements, size_t alignment> inline
typename fb_alloc<T,nof_elements,alignment>::size_type    
fb_alloc<T,nof_elements,alignment>::max_size( void ) const throw()
{
   return elsize_;
}

template <typename T, unsigned int nof_elements, size_t alignment> inline unsigned int 
fb_alloc<T,nof_elements,alignment>::nof_elmts( void ) const
{
   return nof_elmts_;
}

template <typename T, unsigned int nof_elements, size_t alignment> inline size_t
fb_alloc<T,nof_elements,alignment>::elsize( void ) const
{
   return elsize_;
}

template <typename T, unsigned int nof_elements, size_t alignment> inline size_t
fb_alloc<T,nof_elements,alignment>::chunksize( void ) const
{
   return elsize_*nof_elmts_ + sizeof(char*);
}

template <typename T, unsigned int nof_elements, size_t alignment> inline size_t
fb_alloc<T,nof_elements,alignment>::alignmnt( void ) const
{
   return alignment_;
}

template <typename T, unsigned int nof_elements, size_t alignment> inline int
fb_alloc<T,nof_elements,alignment>::refcount( void ) const
{
   if ( refcount_ )
   {
      return *refcount_;
   }
   return 0;  
}

template <typename T, unsigned int nof_elements, size_t alignment> inline int
fb_alloc<T,nof_elements,alignment>::nof_allocs( void ) const
{
   if ( nof_allocs_ )
   {
      return *nof_allocs_;
   }
   return 0;
}

template <typename T, unsigned int nof_elements, size_t alignment>  void
fb_alloc<T,nof_elements,alignment>::dump( std::ostream& os ) const
{
   os << "A: " << nof_allocated_chunks_ << " " << nof_free_chunks_ << std::endl;
}

#endif // FB_ALLOC_H
