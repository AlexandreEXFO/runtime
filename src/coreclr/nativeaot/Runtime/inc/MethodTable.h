// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

// Fundamental runtime type representation

#pragma warning(push)
#pragma warning(disable:4200) // nonstandard extension used : zero-sized array in struct/union
//-------------------------------------------------------------------------------------------------
// Forward declarations

class MethodTable;
class TypeManager;
struct TypeManagerHandle;

//-------------------------------------------------------------------------------------------------
// The subset of TypeFlags that NativeAOT knows about at runtime
// This should match the TypeFlags enum in the managed type system.
enum EETypeElementType : uint8_t
{
    // Primitive
    ElementType_Unknown = 0x00,
    ElementType_Void = 0x01,
    ElementType_Boolean = 0x02,
    ElementType_Char = 0x03,
    ElementType_SByte = 0x04,
    ElementType_Byte = 0x05,
    ElementType_Int16 = 0x06,
    ElementType_UInt16 = 0x07,
    ElementType_Int32 = 0x08,
    ElementType_UInt32 = 0x09,
    ElementType_Int64 = 0x0A,
    ElementType_UInt64 = 0x0B,
    ElementType_IntPtr = 0x0C,
    ElementType_UIntPtr = 0x0D,
    ElementType_Single = 0x0E,
    ElementType_Double = 0x0F,

    ElementType_ValueType = 0x10,
    // Enum = 0x11, // EETypes store enums as their underlying type
    ElementType_Nullable = 0x12,
    // Unused 0x13,

    ElementType_Class = 0x14,
    ElementType_Interface = 0x15,

    ElementType_SystemArray = 0x16, // System.Array type

    ElementType_Array = 0x17,
    ElementType_SzArray = 0x18,
    ElementType_ByRef = 0x19,
    ElementType_Pointer = 0x1A,
    ElementType_FunctionPointer = 0x1B,
};

//-------------------------------------------------------------------------------------------------
// Support for encapsulating the location of fields in the MethodTable that have variable offsets or may be
// optional.
//
// The following enumaration gives symbolic names for these fields and is used with the GetFieldPointer() and
// GetFieldOffset() APIs.
enum EETypeField
{
    ETF_TypeManagerIndirection,
    ETF_WritableData,
    ETF_DispatchMap,
    ETF_Finalizer,
    ETF_SealedVirtualSlots,
    ETF_DynamicTemplateType,
    ETF_GenericDefinition,
    ETF_GenericComposition,
    ETF_FunctionPointerParameters,
    ETF_DynamicTypeFlags,
    ETF_DynamicGcStatics,
    ETF_DynamicNonGcStatics,
    ETF_DynamicThreadStaticOffset,
};

//-------------------------------------------------------------------------------------------------
// Fundamental runtime type representation
typedef DPTR(class MethodTable) PTR_EEType;
typedef DPTR(PTR_EEType) PTR_PTR_EEType;

extern "C" void PopulateDebugHeaders();

class MethodTable
{
    friend class AsmOffsets;
    friend void PopulateDebugHeaders();

private:
    struct RelatedTypeUnion
    {
        union
        {
            // Kinds.CanonicalEEType
            MethodTable*     m_pBaseType;

            // Kinds.ParameterizedEEType
            MethodTable*  m_pRelatedParameterType;
        };
    };

    // native code counterpart for _uFlags
    union
    {
        uint32_t              m_uFlags;
        // lower uint16 of m_uFlags is ComponentSize, when HasComponentSize == true
        // also accessed in asm allocation helpers
        uint16_t              m_usComponentSize;
    };
    uint32_t              m_uBaseSize;
    RelatedTypeUnion      m_RelatedType;
    uint16_t              m_usNumVtableSlots;
    uint16_t              m_usNumInterfaces;
    uint32_t              m_uHashCode;

    TgtPTR_Void         m_VTable[];  // make this explicit so the binder gets the right alignment

    // after the m_usNumVtableSlots vtable slots, we have m_usNumInterfaces slots of
    // MethodTable*, and after that a couple of additional pointers based on whether the type is
    // finalizable (the address of the finalizer code) or has optional fields (pointer to the compacted
    // fields).

    enum Flags
    {
        // There are four kinds of EETypes, the three of them regular types that use the full MethodTable encoding
        // plus a fourth kind used as a grab bag of unusual edge cases which are encoded in a smaller,
        // simplified version of MethodTable. See LimitedEEType definition below.
        EETypeKindMask = 0x00030000,

        // GC depends on this bit, this bit must be zero
        CollectibleFlag         = 0x00200000,

        HasDispatchMapFlag      = 0x00040000,

        IsDynamicTypeFlag       = 0x00080000,

        // GC depends on this bit, this type requires finalization
        HasFinalizerFlag        = 0x00100000,

        HasSealedVTableEntriesFlag = 0x00400000,

        // GC depends on this bit, this type contain gc pointers
        HasPointersFlag         = 0x01000000,

        // This type is generic and one or more of it's type parameters is co- or contra-variant. This only
        // applies to interface and delegate types.
        GenericVarianceFlag     = 0x00800000,

        // This type is generic.
        IsGenericFlag           = 0x02000000,

        // We are storing a EETypeElementType in the upper bits for unboxing enums
        ElementTypeMask      = 0x7C000000,
        ElementTypeShift     = 26,

        // The m_usComponentSize is a number (not holding ExtendedFlags).
        HasComponentSizeFlag = 0x80000000,
    };

    enum ExtendedFlags
    {
        HasEagerFinalizerFlag = 0x0001,
        // GC depends on this bit, this type has a critical finalizer
        HasCriticalFinalizerFlag = 0x0002,
        IsTrackedReferenceWithFinalizerFlag = 0x0004,

        // This MethodTable is for a Byref-like class (TypedReference, Span<T>, ...)
        IsByRefLikeFlag = 0x0010,

        // This type requires 8-byte alignment for its fields on certain platforms (ARM32, WASM)
        RequiresAlign8Flag = 0x1000
    };

    enum FunctionPointerFlags
    {
        IsUnmanaged = 0x80000000,
        FunctionPointerFlagsMask = IsUnmanaged
    };

public:

    enum Kinds
    {
        CanonicalEEType         = 0x00000000,
        FunctionPointerEEType   = 0x00010000,
        ParameterizedEEType     = 0x00020000,
        GenericTypeDefEEType    = 0x00030000,
    };

    uint32_t GetBaseSize()
        { return m_uBaseSize; }

    uint32_t GetNumFunctionPointerParameters()
    {
        ASSERT(IsFunctionPointer());
        return m_uBaseSize & ~FunctionPointerFlagsMask;
    }

    uint32_t GetParameterizedTypeShape()
    {
        ASSERT(IsParameterizedType());
        return m_uBaseSize;
    }

    Kinds GetKind();

    bool IsArray()
    {
        EETypeElementType elementType = GetElementType();
        return elementType == ElementType_Array || elementType == ElementType_SzArray;
    }

    bool IsSzArray()
        { return GetElementType() == ElementType_SzArray; }

    uint32_t GetArrayRank();

    bool IsParameterizedType()
        { return (GetKind() == ParameterizedEEType); }

    bool IsGenericTypeDefinition()
        { return GetKind() == GenericTypeDefEEType; }

    bool IsFunctionPointer()
        { return GetKind() == FunctionPointerEEType; }

    bool IsInterface()
        { return GetElementType() == ElementType_Interface; }

    MethodTable * GetRelatedParameterType();

    MethodTable* GetNonArrayBaseType()
    {
        ASSERT(!IsArray());
        return PTR_EEType(reinterpret_cast<TADDR>(m_RelatedType.m_pBaseType));
    }

    bool IsValueType()
        { return GetElementType() < ElementType_Class; }

    bool HasFinalizer()
    {
        return (m_uFlags & HasFinalizerFlag) != 0;
    }

    bool HasEagerFinalizer()
    {
        return (m_uFlags & HasEagerFinalizerFlag) && !HasComponentSize();
    }

    bool HasCriticalFinalizer()
    {
        return (m_uFlags & HasCriticalFinalizerFlag) && !HasComponentSize();
    }

    bool IsTrackedReferenceWithFinalizer()
    {
        return (m_uFlags & IsTrackedReferenceWithFinalizerFlag) && !HasComponentSize();
    }

    bool  HasComponentSize()
    {
        static_assert(HasComponentSizeFlag == (MethodTable::Flags)(1 << 31), "we assume that HasComponentSizeFlag matches the sign bit");
        // return (m_uFlags & HasComponentSizeFlag) != 0;

        return (int32_t)m_uFlags < 0;
    }

    uint16_t RawGetComponentSize()
    {
        return m_usComponentSize;
    }

    uint16_t GetComponentSize()
    {
        return HasComponentSize() ? RawGetComponentSize() : 0;
    }

    bool HasReferenceFields()
    {
        return (m_uFlags & HasPointersFlag) != 0;
    }

    // How many vtable slots are there?
    uint16_t GetNumVtableSlots()
        { return m_usNumVtableSlots; }

    // How many entries are in the interface map after the vtable slots?
    uint16_t GetNumInterfaces()
        { return m_usNumInterfaces; }

    TypeManagerHandle* GetTypeManagerPtr();

    MethodTable* GetDynamicTemplateType();

    // Used only by GC initialization, this initializes the MethodTable used to mark free entries in the GC heap.
    // It should be an array type with a component size of one (so the GC can easily size it as appropriate)
    // and should be marked as not containing any references. The rest of the fields don't matter: the GC does
    // not query them and the rest of the runtime will never hold a reference to free object.
    inline void InitializeAsGcFreeType();

    // Mark or determine that a type is generic and one or more of it's type parameters is co- or
    // contra-variant. This only applies to interface and delegate types.
    bool HasGenericVariance()
        { return (m_uFlags & GenericVarianceFlag) != 0; }

    EETypeElementType GetElementType()
        { return (EETypeElementType)((m_uFlags & ElementTypeMask) >> ElementTypeShift); }

    bool IsGeneric()
        { return (m_uFlags & IsGenericFlag) != 0; }

    bool HasDispatchMap()
        { return (m_uFlags & HasDispatchMapFlag) != 0; }

    bool HasSealedVTableEntries()
        { return (m_uFlags & HasSealedVTableEntriesFlag) != 0; }

    // Determine whether a type was created by dynamic type loader
    bool IsDynamicType()
        { return (m_uFlags & IsDynamicTypeFlag) != 0; }

    // Helper methods that deal with MethodTable topology (size and field layout). These are useful since as we
    // optimize for pay-for-play we increasingly want to customize exactly what goes into an MethodTable on a
    // per-type basis. The rules that govern this can be both complex and volatile and we risk sprinkling
    // various layout rules through the binder and runtime that obscure the basic meaning of the code and are
    // brittle: easy to overlook when one of the rules changes.
    //
    // The following methods can in some cases have fairly complex argument lists of their own and in that way
    // they expose more of the implementation details than we'd ideally like. But regardless they still serve
    // an arguably more useful purpose: they identify all the places that rely on the MethodTable layout. As we
    // change layout rules we might have to change the arguments to the methods below but in doing so we will
    // instantly identify all the other parts of the binder and runtime that need to be updated.

    // Calculate the offset of a field of the MethodTable that has a variable offset.
    inline uint32_t GetFieldOffset(EETypeField eField);

    // Validate an MethodTable extracted from an object.
    bool Validate(bool assertOnFail = true);

public:
    // Methods expected by the GC
    uint32_t ContainsGCPointers() { return HasReferenceFields(); }
    uint32_t ContainsGCPointersOrCollectible() { return HasReferenceFields(); }
    UInt32_BOOL SanityCheck() { return Validate(); }
};

#pragma warning(pop)
