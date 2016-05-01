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

    namespace detail
    {
        template< class T , class = std::conditional_t< std::is_copy_constructible<T>::value, std::true_type, std::false_type > >
        struct as_copy_constructible_as {};

        template< class T >
        struct as_copy_constructible_as< T, std::false_type >
        {
            as_copy_constructible_as() = default;
            as_copy_constructible_as( const as_copy_constructible_as& ) = delete;
        };

        template< class T, class = std::conditional_t< std::is_copy_assignable<T>::value, std::true_type, std::false_type > >
        struct as_copy_assignable_as {};

        template< class T >
        struct as_copy_assignable_as< T, std::false_type >
        {
            as_copy_assignable_as() = default;
            as_copy_assignable_as& operator=( const as_copy_assignable_as& ) = delete;
        };

        template< class T >
        struct as_copyable_as
            : as_copy_constructible_as<T>
            , as_copy_assignable_as<T>
        {};
    }

    template< class ConceptType
        , template< class... > class ModelType
        , size_t BUFFER_SIZE
        , class OwningPtrType
        , class ObserverPtrType
    >
    class polymorphic_storage
        //: detail:: as_copyable_as< OwningPtrType >
    {
    public:
        using ObserverPtr = ObserverPtrType;
        using OwningPtr = OwningPtrType;
        using this_type = polymorphic_storage< ConceptType, ModelType, BUFFER_SIZE, OwningPtrType, ObserverPtrType >;

        polymorphic_storage() = default;

        polymorphic_storage( const polymorphic_storage& other )
            : data( other.data )
        {
            static_assert( std::is_copy_constructible_v<OwningPtr>
                , "This type is not copy-constructible because it can contain an owning pointer which is not copy constructible." );
        }

        polymorphic_storage& operator=( const polymorphic_storage& other )
        {
            data = other.data;
            static_assert( std::is_copy_assignable_v<OwningPtr>
                , "This type is not copy-assignable because it can contain an owning pointer which is not copy assignable." );
        }

        polymorphic_storage( polymorphic_storage&& other
            , typename std::enable_if<std::is_move_constructible<OwningPtrType>::value>::type* = nullptr ) noexcept
            : data( std::move( other.data ) ) {}

        auto operator=( polymorphic_storage&& other ) noexcept
            -> typename std::enable_if< std::is_move_assignable<OwningPtrType>::value, polymorphic_storage& >::type
        {
            data = std::move( other.data );
            return *this;
        }

        template< class Arg
            , class = std::enable_if<  !std::is_same< this_type, typename std::decay<Arg>::type >::value
                                    && !std::is_same< OwningPtr, typename std::decay<Arg>::type >::value
                                    && !std::is_same< ObserverPtr, typename std::decay<Arg>::type >::value
                                    >::type
        >
        polymorphic_storage( Arg&& object )
        {
            store( std::forward<Arg>( object ), fit_in_buffer<sizeof( Arg ), BUFFER_SIZE>{} );
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
            data = std::move( obs_ptr );
            return *this;
        }

        polymorphic_storage& operator=( ObserverPtr obs_potr ) noexcept
        {
            data = std::move( own_ptr );
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

        bool is_buffered_value() const { return !empty() && boost::apply_visitor( BufferedVisitor{}, data ); }

    private:
        using Buffer = std::array<char, BUFFER_SIZE>;
        boost::variant< ObserverPtr, OwningPtr, Buffer > data = static_cast<ObserverPtr>(nullptr);

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

        struct BufferedVisitor : boost::static_visitor<bool>
        {
            template< class T >
            bool operator()( const T& storage ) const noexcept
            {
                return false;
            }

            bool operator()( const Buffer& observer_ptr ) const noexcept
            {
                return true;
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
    using enable_if_different = std::enable_if_t< !std::is_same< std::decay_t<Left>, std::decay_t<Right> >::value >;

}

namespace lol
{

    class Foo
    {
    public:
        Foo() = default;

        template< class T, class = util::enable_if_different<Foo, T> >
        Foo( T&& other )
            : stored( Model<T>( std::forward<T>( other ) ) )
        {
        }

        int bar() { return stored->bar(); }
        void yop() { return stored->yop(); }

        bool has_ownership() const { return stored.has_ownership(); }
        bool empty() const { return stored.empty(); }
        explicit operator bool() const { return stored ? true : false; }
        bool is_buffered_value() const { return stored.is_buffered_value(); }
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
            Model( InitialState&& initial_state, util::enable_if_different<Foo, T>* = nullptr )
                : object( std::forward<InitialState>( initial_state ) )
            {}

            Model( const Model& ) = default;
            Model& operator=( const Model& ) = default;

            Model( Model&& other ) noexcept = default;
            Model& operator=( Model&& other ) noexcept = default;

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


    struct Massive
    {
        std::array<int, 100> values;
        int bar() { return 1234; }

        void yop()
        {
            std::cout << "YOP(Massive)" << std::endl;
        }
    };


}

template< class T >
struct ShowMe;

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

        //lol::Foo u = f; // SHOULD NEVER COMPILE IF Foo have unique storage

        auto k = std::move(f);
        std::cout << k.bar() << std::endl;
        k.yop();

        assert( !f );

        f = my::Massive{};
        std::cout << f.bar() << std::endl;
        f.yop();

        f = std::move(k);
        std::cout << f.bar() << std::endl;
        f.yop();

        assert( !k );


    }

    /*typename util::enable_if_different< lol::Foo, int >* k = nullptr;
    ShowMe<decltype( k )> mofo;
*/

    //lol::Foo foo;
    //lol::Foo bar = std::move(foo);
    //bar = foo;

    std::cin.ignore();
}
