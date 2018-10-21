#pragma once
#include "Serialization.h"

enum ReferenceType
{
	REF_EMPTY,
	REF_CLONE,
	REF_ASSEMBLY, //index = assembly type/function array index
	REF_IMPORT, //index = import #
	REF_ARGUMENT, //index = generic parameter list index
	REF_CLONETYPE,
	//For REF_Assembly and REF_Import, the items after this item is the generic arguments
};
//Note that for generic function, the generic arguments should use REF_CloneType

struct GenericParameter
{
};
FIELD_SERIALIZER_BEGIN(GenericParameter)
FIELD_SERIALIZER_END()

struct DeclarationReference
{
	ReferenceType Type;
	std::size_t Index;
};
FIELD_SERIALIZER_BEGIN(DeclarationReference)
	SERIALIZE_FIELD(Type)
	SERIALIZE_FIELD(Index)
FIELD_SERIALIZER_END()

struct GenericDeclaration
{
	std::vector<GenericParameter> Parameters;
	std::vector<DeclarationReference> Types;
	std::vector<DeclarationReference> Functions;
};
FIELD_SERIALIZER_BEGIN(GenericDeclaration)
	SERIALIZE_FIELD(Parameters)
	SERIALIZE_FIELD(Types)
	SERIALIZE_FIELD(Functions)
FIELD_SERIALIZER_END()
