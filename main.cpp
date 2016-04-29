#include <iostream>

#include <cassert>
#include <array>
#include <type_traits>
#include <boost/variant.hpp>

/*
TODO:
 - instead of having ownership arguments, just manage all the possible pointer types
 - for unique_ptr, store it in a shared_ptr internally, with a way to prevent copies
 - allow storing std::ref
 - in-place construction of objects

*/

namespace util
{

    template< size_t object_size, size_t buffer_size >
    using fit_in_buffer = typename std::conditional< buffer_size >= object_size
        , std::true_type
        , std::false_type
    >::type;

    template< class ConceptType
        , template< class... > class ModelType
        , size_t BUFFER_SIZE
        , class OwningPtrType
        , class ObserverPtrType
    >
    class polymorphic_storage
    {
    public:
        using ObserverPtr = ObserverPtrType;
        using OwningPtr = OwningPtrType;

        polymorphic_storage() = default;

        polymorphic_storage( const polymorphic_storage& ) = default;
        polymorphic_storage& operator=( const polymorphic_storage& ) = default;

        polymorphic_storage( polymorphic_storage&& other, typename std::enable_if<std::is_move_constructible<OwningPtrType>::value>::type* = nullptr ) noexcept
            : data( std::move( other.data ) ) {}

        auto operator=( polymorphic_storage&& other ) noexcept -> typename std::enable_if< std::is_move_assignable<OwningPtrType>::value, polymorphic_storage& >::type
        {
            data = std::move( other.data ); return *this;
        }

        template< class T >
        polymorphic_storage( T&& object
            , typename std::enable_if<  !std::is_same< polymorphic_storage, typename std::decay<T>::type >::value
            && !std::is_same<OwningPtr, typename std::decay<T>::type >::value
            && !std::is_same<ObserverPtr, typename std::decay<T>::type >::value
            >::type* = nullptr )
        {
            store( std::forward<T>( object ), fit_in_buffer<sizeof( T ), BUFFER_SIZE>{} );
        }

        polymorphic_storage( OwningPtr own_ptr )
            : data( std::move( own_ptr ) )
        {
        }

        polymorphic_storage( ObserverPtr obs_ptr )
            : data( std::move( obs_ptr ) )
        {
        }

        ~polymorphic_storage()
        {
            destroy();
        }

        template< class T >
        auto operator=( T&& other )
            -> typename std::enable_if<  !std::is_same<polymorphic_storage, typename std::decay<T>::type >::value
            && !std::is_same<OwningPtr, typename std::decay<T>::type >::value
            && !std::is_same<ObserverPtr, typename std::decay<T>::type >::value
            , polymorphic_storage&
            >::type
        {
            store( std::forward<T>( other ), fit_in_buffer<sizeof( T ), BUFFER_SIZE>{} );
            return *this;
        }

        polymorphic_storage& operator=( OwningPtr obs_ptr ) noexcept
        {
            data( std::move( obs_ptr ) )
                return *this;
        }

        polymorphic_storage& operator=( ObserverPtr obs_potr ) noexcept
        {
            data( std::move( own_ptr ) )
                return *this;
        }

        const ConceptType* operator->() const
        {
            return &object();
        }

        ConceptType* operator->()
        {
            return &object();
        }

        const ConceptType& operator*() const
        {
            return object();
        }

        ConceptType& operator*()
        {
            return object();
        }

        bool empty() const { return boost::apply_visitor( EmptynessVisitor{}, data ); }
        bool has_ownership() const { return boost::apply_visitor( OwnershipVisitor{}, data ); }

        explicit operator bool() const { return !empty(); }

    private:
        using Buffer = std::array<char, BUFFER_SIZE>;
        boost::variant<ObserverPtr, OwningPtr, Buffer > data = nullptr;

        static ConceptType* as_object_ptr( Buffer& buffer ) noexcept
        {
            return reinterpret_cast<ConceptType*>( buffer.data() );
        }

        struct EmptynessVisitor : boost::static_visitor<bool>
        {
            bool operator()( const Buffer& buffer ) const noexcept
            {
                return buffer.empty();
            }

            bool operator()( const OwningPtr& owning_ptr ) const noexcept
            {
                return owning_ptr ? false : true;
            }

            bool operator()( const ObserverPtr& observer_ptr ) const noexcept
            {
                return observer_ptr == nullptr;
            }
        };

        struct OwnershipVisitor : boost::static_visitor<bool>
        {
            template< class T >
            bool operator()( const T& storage ) const noexcept
            {
                return true;
            }

            bool operator()( const ObserverPtr& observer_ptr ) const noexcept
            {
                return false;
            }
        };

        struct ObjectVisitor : boost::static_visitor<ConceptType&>
        {
            ConceptType& operator()( Buffer& buffer ) const noexcept
            {
                return *as_object_ptr( buffer );
            }
            ConceptType& operator()( OwningPtr& ptr ) const noexcept
            {
                return *ptr;
            }
            ConceptType& operator()( ObserverPtr& ptr ) const noexcept
            {
                return *ptr;
            }
        };

        struct DestroyVisitor : boost::static_visitor<void>
        {
            void operator()( Buffer& buffer ) const noexcept
            {
                auto* object_ptr = as_object_ptr( buffer );
                object_ptr->~ConceptType();
            }
            void operator()( OwningPtr& ptr ) const noexcept
            {
                ptr = {};
            }
            void operator()( ObserverPtr& ptr ) const noexcept
            {
                ptr = {};
            }
        };

        ConceptType& object() { return boost::apply_visitor( ObjectVisitor{}, data ); }
        const ConceptType& object() const { return boost::apply_visitor( ObjectVisitor{}, data ); }

        template<class T>
        void store( T&& object, std::false_type )
        {
            data = OwningPtr{ new ModelType<T>( std::forward<T>( object ) ) };
        }

        template<class T>
        void store( T&& object, std::true_type )
        {
            auto& buffer = boost::get<Buffer>( data );
            void* raw_buffer = static_cast<void*>( buffer.data() );
            new( raw_buffer ) ModelType<T>( std::forward<T>( object ) );
        }

        void destroy()
        {
            boost::apply_visitor( DestroyVisitor{}, data );
        }
    };

    template< class ConceptType, template<class...> class ModelType, size_t BUFFER_SIZE = sizeof( void* ) >
    using unique_poly_storage = polymorphic_storage<ConceptType, ModelType, BUFFER_SIZE, std::unique_ptr<ConceptType>, ConceptType* >;

    template< class ConceptType, template<class...> class ModelType, size_t BUFFER_SIZE = sizeof( void* ) >
    using shared_poly_storage = polymorphic_storage<ConceptType, ModelType, BUFFER_SIZE, std::shared_ptr<ConceptType>, ConceptType* >;

    //template< class ConceptType, template<...> class ModelType, size_t BUFFER_SIZE = sizeof(void*) >
    //using value_poly_storage = polymorphic_storage<const ConceptType, ModelType, BUFFER_SIZE, std::shared_ptr<const ConceptType>, const ConceptType* >;

    template< class Left, class Right >
    struct enable_if_different : std::enable_if_t< !std::is_same_v< std::decay_t<Left>, std::decay_t<Right> > >
    {};

}

namespace lol
{

    class Foo
    {
    public:
        Foo() = default;

        template< class T>
        Foo( T&& other, util::enable_if_different<Foo, T>* = nullptr )
            : stored( Model<T>( std::forward<T>( other ) ) )
        {
        }

        int bar() { return stored->bar(); }
        void yop() { return stored->yop(); }

        bool has_ownership() const { return stored.has_ownership(); }
        bool empty() const { return stored.empty(); }
        explicit operator bool() const { return stored ? true : false; }

    private:
        struct Concept
        {
            virtual ~Concept() = default;
            virtual int bar() = 0;
            virtual void yop() = 0;
        };

        template< class T >
        class Model : public Concept
        {
            T object;
        public:
            template<class InitialState>
            Model( InitialState&& initial_state, util::enable_if_different<Foo, T> * = nullptr )
                : object( std::forward<InitialState>( initial_state ) )
            {}

            Model( const Model& ) = default;
            Model& operator=( const Model& ) = default;

            Model( Model&& other, typename std::enable_if<std::is_move_constructible<T>::value>::type* = nullptr ) noexcept
                : object( std::move( other.object ) ) {  }

            auto operator=( Model&& other ) noexcept -> typename std::enable_if<std::is_move_assignable<T>::value, Model&>::type
            {
                object = std::move( other.object );
                return *this;
            }

            int bar() override { return object.bar(); }
            void yop() override { return object.yop(); }
        };

        util::unique_poly_storage< Concept, Model > stored;
    };

}

namespace my
{
    struct Blah
    {
        int bar() { return 42; }

        void yop()
        {
            std::cout << "YOP(blah)" << std::endl;
        }
    };


}

int main()
{
    {
        lol::Foo f;
        assert( f.empty() );
        assert( !f );
        assert( !f.has_ownership() );

        f = my::Blah{};
        assert( !f.empty() );
        assert( f );
        assert( f.has_ownership() );
    }

    {
        lol::Foo f = my::Blah{};
        assert( !f.empty() );
        assert( f );
        assert( f.has_ownership() );
    }

    {
        lol::Foo f(my::Blah{});
        assert( !f.empty() );
        assert( f );
        assert( f.has_ownership() );
    }

    {
        lol::Foo f = my::Blah{};
        std::cout << f.bar() << std::endl;
        f.yop();
    }

}
