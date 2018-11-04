#pragma once
#include <vector>
#include <deque>
#include <cassert>
#include <memory>
#include <algorithm>
#include "Assembly.h"
#include "Spinlock.h"
#include "LoaderObjects.h"

//TODO support base class (including virtual function support)
/*

	Basic ideas:
	1. Base class is constructed after the construction of derived type but
	   must be loaded (up to LoadFields) before. This avoids the need to check
	   cyclic base class separately. Interface can be loaded later. 
	2. Abstract generic class can have a 'derived' argument
		abstract class Animal<derived T> ...
		This is a front-end feature. We don't need to care too much here...
	3. Virtual function call is done through a pointer to global storage.
	4. Each class assign a global type for vtab. It will be automatically
	   included in the RuntimeObject layout. The type of functions are checked
	   when loading to match the base class.
	-. Allow pointer to a global storage type.
	6. Native type for managed function only with generic.
		Maybe we need to have a RefParam<T> type besides Pointer<T> to indicate
		it's a ref parameter (so we can do some optimization/transformation).
	7. Unmanaged function pointer don't need to be wrapped to function type.
	   Delegate like C# should be fine. Managed function type is only internal use.

*/
//TODO Allow value type to implement interface?
//TODO pay attention to the following C# code (note it's not possible to do the same in C++)
/*
		class A<T> : List<A<List<T>>>
		{
		}
		A<int> x;
		void X()
		{
			var val = x[0][0][0][0][0][0][0][0][0][0][0];
		}
*/
//Two solusion:
//1. Disallow (by limiting simultaneously loading types)
//2. Allow incomplete type. Only call LoadFields when needed. (Need major change in structure.)


/*
Traits: we want the following:

//T is the type that should have all the requirements, TElement is generic parameter
traits TestTraits<TElement>(T)
{
	//Requires self type to have function/field
	bool MoveNext(); //Normal function
	TElement Current_get(); //Referencing a generic parameter
	static int Compare(T, T); //Referencing self
	void AddList(List<T>); //Equals to: requires _someType = List<T>; void AddList(_someType);
	int Length; //Field (need field table)

	//Requires another type to exist
	requires _intType = T::IntType; //An alias. Scope of _intType is within this traits definition
	requires T::TemplateType<int>; //A template alias
	requires List<T>; //Another type

	//Refer to another traits
	requires _intType : ValueType;
	requires _intType : new(); //System defined traits Constructible()

	//Requires another type to have function
	//Compiles to a new traits and a declaration of Stream on that traits
	requires Stream
	{
		//Note that in the case Stream.Write has multiple overload and containing a Write(object),
		//the exact overload is chosen for each T, instead of always use Write(object). See below.
		void Write(T);
	}
}


traits _someAutoGeneratedTraits<T1>(T)
{
	void Write(T1);
}

traits TestTraits(T)
{
	requires Stream : _someAutoGeneratedTraits<T>;
}

*/

/*

Parameter pack

Can only contain one parameter pack (don't need to be the last)

void F1<T1, T2...>(T1 a, Tuple<T2>... b);
void F2<T1, T2...>(T1 a, Tuple<T2...> b);

Generic parameter:
[0] T1
[1] T2
---
[2] T2...

Tuple<T2>... = Tuple<[1]>
Tuple<T2...> = Tuple<[2]>

void F1<T1..., T2>(T2 a, Tuple<T1>... b);
void F2<T1..., T2>(T2 a, Tuple<T1...> b);

Generic parameter:
[0] T1
[1] T2
---
[2] T1...

Tuple<T1>... = Tuple<[0]>
Tuple<T1...> = Tuple<[2]>

Because constrains can only be applied on single type, it can only be applied to [0] and [1].

*/

//Roadmap (common)
//TODO Test cases for type loading with base/interfaces, box type
//TODO Generic argument constrain (base class, interface)
//TODO Sub-type alias definition & sub-type reference in GenericDeclaration
//TODO Traits (see above)
//TODO Reference of trait function, type
//TODO Type parital specialization
//TODO GenericDeclaration with parameter pack (see above)

//Roadmap (interpreter)
//TODO RuntimeObject layout

//Note: vtab will be copied from StaticPointer for each type using it, after initializer is executed. 
//Further modification will not have any effect.
//First ? bit in interface vtab is the offset, which is not included in the TSM_GLOBAL type layout,
//but is automatically added. Field (function/const) id counts from the next (first) field.
//Type layout (the position of vtab) can only be controlled by RuntimeLoader. Platform might be allowed
//to specify whether to expand the vtab content (so we don't need a pointer).

/*

----------------------------------------------
Common Header <- Class/base class Ptr
----------------------------------------------
Virtual Table Ptr
	Base virtual function 1
Base Interface A Table Ptr <- Interface A Ptr
	This offset
	Function A1 dest
Base Field A
Base Field B
----------------------------------------------
Interface B Table Ptr <- Interface B Ptr
	This offset
	Function B1 dest
Field C
Field D
----------------------------------------------

*/

class RuntimeLoader
{
	/*
	 * About the loading process:
	 *
	 * Each reference type will undergo the following stages:
	 * 1. LoadTypeInternal. Then move to _loadingRefTypes. Pointer available.
	 * 2. LoadFields. Then move to _postLoadingTypes.
	 * 3. PostLoadType. Then move to _finishedLoadingTypes.
	 * 4. (After all finished.) MoveFinishedObjects. Then move to _loadedTypes.
	 *
	 * Each value type will undergo the following stages:
	 * 1. LoadTypeInternal -> LoadFields.
	 *    Then move to _postLoadingTypes. Pointer available.
	 * 2. PostLoadType. Then move to _finishedLoadingTypes.
	 * 3. (After all finished.) MoveFinishedObjects. Then move to _loadedTypes.
	 *
	 * Each function will undergo the following stages:
	 * 1. LoadFunctionInternal. Then move to _loadingFunctions. Pointer available.
	 * 2. PostLoadFunction. Then move to _finishedLoadingFunctions.
	 * 3. (After all finished.) MoveFinishedObjects. Then move to _loadedFunctions.
	 *
	 * OnXXXLoaded virtual functions are called within MoveFinishedObjects to allow
	 * subclasses to do custom registration work. After all objects are processed,
	 * all objects are moved to loaded list. If any function call fails by throwing
	 * an InternalException, no object will be moved to loaded list and the API fails.
	 *
	 */

protected:
	//Exception thrown internally within RuntimeLoader (and subclasses).
	class RuntimeLoaderException : public std::runtime_error
	{
	public:
		RuntimeLoaderException(const std::string& msg)
			: std::runtime_error(msg)
		{
		}
	};

public:
	RuntimeLoader(AssemblyList assemblies, std::size_t ptrSize = sizeof(void*),
		std::size_t itabPtrSize = sizeof(void*), std::size_t loadingLimit = 256)
		: _assemblies(std::move(assemblies)), _ptrSize(ptrSize),
		_itabPtrSize(itabPtrSize), _loadingLimit(loadingLimit)
	{
		FindInternalTypeId();
	}

	virtual ~RuntimeLoader() {}

public:
	RuntimeType* GetType(const LoadingArguments& args, std::string& err)
	{
		std::lock_guard<Spinlock> lock(_loaderLock);
		for (auto& t : _loadedTypes)
		{
			if (t && t->Args == args)
			{
				return t.get();
			}
		}
		return LoadTypeNoLock(args, err);
	}

	RuntimeFunction* GetFunction(const LoadingArguments& args, std::string& err)
	{
		std::lock_guard<Spinlock> lock(_loaderLock);
		for (auto& f : _loadedFunctions)
		{
			if (f->Args == args)
			{
				return f.get();
			}
		}
		return LoadFunctionNoLock(args, err);
	}

	RuntimeType* AddNativeType(const std::string& assemblyName, const std::string& name,
		std::size_t size, std::size_t alignment, std::string& err)
	{
		std::lock_guard<Spinlock> lock(_loaderLock);
		try
		{
			auto id = FindNativeIdThrow(FindAssemblyThrow(assemblyName)->NativeTypes, name);
			return AddNativeTypeInternal(assemblyName, id, size, alignment);
		}
		catch (std::exception& ex)
		{
			err = ex.what();
		}
		catch (...)
		{
			err = "Unknown exception in adding native type";
		}
		return nullptr;
	}

public:
	RuntimeType* GetTypeById(std::uint32_t id)
	{
		std::lock_guard<Spinlock> lock(_loaderLock);
		if (id >= _loadedTypes.size())
		{
			return nullptr;
		}
		return _loadedTypes[id].get();
	}

	RuntimeFunction* GetFunctionById(std::uint32_t id)
	{
		std::lock_guard<Spinlock> lock(_loaderLock);
		if (id > _loadedFunctions.size())
		{
			return nullptr;
		}
		return _loadedFunctions[id].get();
	}

	bool FindExportType(const AssemblyImport& args, LoadingArguments& result)
	{
		auto a = FindAssemblyThrow(args.AssemblyName);
		for (auto& e : a->ExportTypes)
		{
			if (e.ExportName == args.ImportName)
			{
				if (e.InternalId >= a->Types.size())
				{
					auto importId = e.InternalId - a->Types.size();
					if (importId >= a->ImportTypes.size())
					{
						return false;
					}
					return FindExportType(a->ImportTypes[importId], result);
				}
				if (args.GenericParameters != SIZE_MAX &&
					a->Types[e.InternalId].Generic.Parameters.size() != args.GenericParameters)
				{
					return false;
				}
				result.Assembly = args.AssemblyName;
				result.Id = e.InternalId;
				return true;
			}
		}
		return false;
	}

	bool FindExportFunction(const AssemblyImport& args, LoadingArguments& result)
	{
		auto a = FindAssemblyThrow(args.AssemblyName);
		for (auto& e : a->ExportFunctions)
		{
			if (e.ExportName == args.ImportName)
			{
				if (e.InternalId >= a->Functions.size())
				{
					auto importId = e.InternalId - a->Functions.size();
					if (importId >= a->ImportFunctions.size())
					{
						return false;
					}
					return FindExportFunction(a->ImportFunctions[importId], result);
				}
				if (args.GenericParameters != SIZE_MAX &&
					a->Functions[e.InternalId].Generic.Parameters.size() != args.GenericParameters)
				{
					return false;
				}
				result.Assembly = args.AssemblyName;
				result.Id = e.InternalId;
				return true;
			}
		}
		return false;
	}

	std::uint32_t FindExportConstant(const std::string& assemblyName, const std::string& n)
	{
		auto a = FindAssemblyThrow(assemblyName);
		for (auto& e : a->ExportConstants)
		{
			if (e.ExportName == n) return (std::uint32_t)e.InternalId;
		}
		throw RuntimeLoaderException("Constant export not found");
	}

	std::size_t GetPointerSize()
	{
		return _ptrSize;
	}

private:
	RuntimeType* LoadTypeNoLock(const LoadingArguments& args, std::string& err)
	{
		ClearLoadingLists();
		RuntimeType* ret = nullptr;
		try
		{
			auto ret2 = LoadTypeInternal(args);
			ProcessLoadingLists();
			MoveFinishedObjects();
			ret = ret2;
		}
		catch (std::exception& ex)
		{
			err = ex.what();
		}
		catch (...)
		{
			err = "Unknown exception in loading type";
		}
		ClearLoadingLists();
		return ret;
	}

	RuntimeFunction* LoadFunctionNoLock(const LoadingArguments& args, std::string& err)
	{
		ClearLoadingLists();
		RuntimeFunction* ret = nullptr;
		try
		{
			auto ret2 = LoadFunctionInternal(args);
			ProcessLoadingLists();
			MoveFinishedObjects();
			ret = ret2;
		}
		catch (std::exception& ex)
		{
			err = ex.what();
		}
		catch (...)
		{
			err = "Unknown exception in loading function";
		}
		ClearLoadingLists();
		return ret;
	}

	std::uint32_t LoadImportConstant(Assembly* a, std::size_t index)
	{
		if (index >= a->ImportConstants.size())
		{
			throw RuntimeLoaderException("Invalid constant import reference");
		}
		auto info = a->ImportConstants[index];
		if (info.GenericParameters != 0)
		{
			throw RuntimeLoaderException("Invalid constant import");
		}
		return FindExportConstant(info.AssemblyName, info.ImportName);
	}

private:
	RuntimeType* AddNativeTypeInternal(const std::string& assemblyName, std::size_t id,
		std::size_t size, std::size_t alignment)
	{
		auto a = FindAssemblyThrow(assemblyName);
		auto& type = a->Types[id];
		if (type.Generic.Parameters.size())
		{
			throw RuntimeLoaderException("Native type cannot be generic");
		}
		if (type.GCMode != TSM_VALUE)
		{
			throw RuntimeLoaderException("Internal type can only be value type");
		}
		if (type.Finalizer >= type.Generic.Functions.size())
		{
			throw RuntimeLoaderException("Invalid function reference");
		}
		if (type.Generic.Functions[type.Finalizer].Type != REF_EMPTY)
		{
			throw RuntimeLoaderException("Internal type cannot have finalizer");
		}
		if (type.Initializer >= type.Generic.Functions.size())
		{
			throw RuntimeLoaderException("Invalid function reference");
		}
		if (type.Generic.Functions[type.Initializer].Type != REF_EMPTY)
		{
			throw RuntimeLoaderException("Internal type cannot have initializer");
		}
		auto rt = std::make_unique<RuntimeType>();
		rt->Parent = this;
		rt->TypeId = _nextTypeId++;
		rt->Args.Assembly = assemblyName;
		rt->Args.Id = id;
		rt->Storage = TSM_VALUE;
		rt->Size = size;
		rt->Alignment = alignment;
		rt->Initializer = nullptr;
		rt->Finalizer = nullptr;
		rt->VirtualTableType = nullptr;

		auto ret = rt.get();
		FinalCheckType(ret);
		OnTypeLoaded(ret);
		AddLoadedType(std::move(rt));
		return ret;
	}

	void ClearLoadingLists()
	{
		_loadingTypes.clear();
		_loadingFunctions.clear();
		_loadingRefTypes.clear();
		_postLoadingTypes.clear();
		_finishedLoadingTypes.clear();
		_finishedLoadingFunctions.clear();
	}

	void MoveFinishedObjects()
	{
		for (auto& t : _finishedLoadingTypes)
		{
			FinalCheckType(t.get());
			OnTypeLoaded(t.get());
		}
		for (auto& f : _finishedLoadingFunctions)
		{
			FinalCheckFunction(f.get());
			OnFunctionLoaded(f.get());
		}
		while (_finishedLoadingTypes.size() > 0)
		{
			auto t = std::move(_finishedLoadingTypes.front());
			AddLoadedType(std::move(t));
			_finishedLoadingTypes.pop_front();
		}
		while (_finishedLoadingFunctions.size() > 0)
		{
			auto f = std::move(_finishedLoadingFunctions.front());
			AddLoadedFunction(std::move(f));
			_finishedLoadingFunctions.pop_front();
		}
	}

	void ProcessLoadingLists()
	{
		assert(_loadingTypes.size() == 0);

		while (true)
		{
			if (_loadingRefTypes.size())
			{
				auto t = std::move(_loadingRefTypes.front());
				_loadingRefTypes.pop_front();
				LoadFields(std::move(t), nullptr);
				assert(_loadingTypes.size() == 0);
				continue;
			}
			if (_postLoadingTypes.size())
			{
				auto t = std::move(_postLoadingTypes.front());
				_postLoadingTypes.pop_front();
				PostLoadType(std::move(t));
				assert(_loadingTypes.size() == 0);
				continue;
			}
			if (_loadingFunctions.size())
			{
				auto t = std::move(_loadingFunctions.front());
				_loadingFunctions.pop_front();
				PostLoadFunction(std::move(t));
				assert(_loadingTypes.size() == 0);
				continue;
			}
			break;
		}
	}

	void CheckGenericArguments(GenericDeclaration& g, const LoadingArguments& args)
	{
		if (g.Parameters.size() != args.Arguments.size())
		{
			throw RuntimeLoaderException("Invalid generic arguments");
		}
		if (std::any_of(args.Arguments.begin(), args.Arguments.end(),
			[](RuntimeType* t) { return t == nullptr; }))
		{
			throw RuntimeLoaderException("Invalid generic arguments");
		}
		//TODO argument constrain check
	}

	RuntimeType* LoadTypeInternal(const LoadingArguments& args)
	{
		for (auto& t : _loadedTypes)
		{
			if (t && t->Args == args)
			{
				return t.get();
			}
		}
		for (auto& t : _finishedLoadingTypes)
		{
			if (t->Args == args)
			{
				return t.get();
			}
		}
		for (auto& t : _postLoadingTypes)
		{
			if (t->Args == args)
			{
				return t.get();
			}
		}
		for (auto& t : _loadingRefTypes)
		{
			if (t->Args == args)
			{
				return t.get();
			}
		}
		for (auto t : _loadingTypes)
		{
			if (t->Args == args)
			{
				return t;
			}
		}

		auto typeTemplate = FindTypeTemplate(args.Assembly, args.Id);
		CheckGenericArguments(typeTemplate->Generic, args);
		if (typeTemplate->Generic.Fields.size() != 0)
		{
			throw RuntimeLoaderException("Type template cannot contain field reference");
		}

		if (args.Assembly == "Core" && args.Id == _boxTypeId)
		{
			if (args.Arguments.size() != 1 ||
				args.Arguments[0]->Storage != TSM_VALUE)
			{
				throw RuntimeLoaderException("Box type can only take value type as argument");
			}
		}

		auto t = std::make_unique<RuntimeType>();
		t->Parent = this;
		t->Args = args;
		t->TypeId = _nextTypeId++;
		t->Storage = typeTemplate->GCMode;
		t->PointerType = nullptr;

		if (typeTemplate->GCMode == TSM_REFERENCE || typeTemplate->GCMode == TSM_INTERFACE)
		{
			RuntimeType* ret = t.get();
			_loadingRefTypes.push_back(std::move(t));
			return ret;
		}
		else
		{
			return LoadFields(std::move(t), typeTemplate);
		}
	}

	RuntimeFunction* LoadFunctionInternal(const LoadingArguments& args)
	{
		for (auto& ff : _loadedFunctions)
		{
			if (ff && ff->Args == args)
			{
				return ff.get();
			}
		}
		for (auto& ff : _finishedLoadingFunctions)
		{
			if (ff->Args == args)
			{
				return ff.get();
			}
		}
		for (auto& ff : _loadingFunctions)
		{
			if (ff->Args == args)
			{
				return ff.get();
			}
		}

		auto funcTemplate = FindFunctionTemplate(args.Assembly, args.Id);
		CheckGenericArguments(funcTemplate->Generic, args);

		auto f = std::make_unique<RuntimeFunction>();
		auto ret = f.get();
		_loadingFunctions.push_back(std::move(f));
		ret->Args = args;
		ret->Parent = this;
		ret->FunctionId = _nextFunctionId++;
		ret->Code = GetCode(args.Assembly, args.Id);
		return ret;
	}

	bool TypeIsInLoading(RuntimeType* t)
	{
		for (auto i : _loadingTypes)
		{
			if (i == t) return true;
		}
		return false;
	}

	RuntimeType* LoadFields(std::unique_ptr<RuntimeType> type, Type* typeTemplate)
	{
		for (auto t : _loadingTypes)
		{
			assert(!(t->Args == type->Args));
		}
		_loadingTypes.push_back(type.get());
		if (_loadingTypes.size() + _loadingFunctions.size() > _loadingLimit)
		{
			throw RuntimeLoaderException("Loading object limit exceeded.");
		}

		Type* tt = typeTemplate;
		if (tt == nullptr)
		{
			tt = FindTypeTemplate(type->Args.Assembly, type->Args.Id);
		}

		if (type->Storage == TSM_INTERFACE)
		{
			if (tt->Fields.size() != 0)
			{
				throw RuntimeLoaderException("Interface cannot have fields");
			}
		}

		//Virtual table
		auto vtabType = LoadRefType({ type.get(), tt->Generic }, tt->Base.VirtualTableType);
		if (vtabType != nullptr)
		{
			if (vtabType->Storage != TSM_GLOBAL)
			{
				throw RuntimeLoaderException("Vtab type must be global storage");
			}
			if (type->Storage == TSM_GLOBAL || type->Storage == TSM_VALUE)
			{
				throw RuntimeLoaderException("Global and value type cannot have vtab");
			}

			type->VirtualTableType = vtabType;
		}
		else
		{
			if (type->Storage == TSM_INTERFACE)
			{
				throw RuntimeLoaderException("Interface must have vtab");
			}
		}

		//Base type
		auto baseType = LoadRefType({ type.get(), tt->Generic }, tt->Base.InheritedType);
		if (baseType != nullptr)
		{
			if (type->Storage == TSM_GLOBAL)
			{
				throw RuntimeLoaderException("Global type cannot have base type");
			}
			else if (type->Storage == TSM_INTERFACE)
			{
				throw RuntimeLoaderException("Interface cannot have base type");
			}
			else
			{
				if (type->Storage != baseType->Storage)
				{
					throw RuntimeLoaderException("Base type storage must be same as the derived type");
				}
			}
			type->BaseType = baseType;
		}
		CheckVirtualTable(baseType, vtabType);

		std::size_t offset = 0, totalAlignment = 1;

		//Fields
		std::vector<RuntimeType*> fields;
		for (auto typeId : tt->Fields)
		{
			auto fieldType = LoadRefType({ type.get(), tt->Generic }, typeId);
			if (fieldType == nullptr)
			{
				//Only goes here if REF_EMPTY is specified.
				throw RuntimeLoaderException("Invalid field type");
			}
			if (fieldType->Storage == TSM_VALUE && fieldType->Alignment == 0)
			{
				assert(TypeIsInLoading(fieldType));
				throw RuntimeLoaderException("Cyclic type dependence");
			}
			fields.push_back(fieldType);
		}

		for (std::size_t i = 0; i < fields.size(); ++i)
		{
			auto ftype = fields[i];
			std::size_t len, alignment;
			switch (ftype->Storage)
			{
			case TSM_REFERENCE:
			case TSM_INTERFACE:
				len = alignment = _ptrSize;
				break;
			case TSM_VALUE:
				len = ftype->Size;
				alignment = ftype->Alignment;
				break;
			default:
				throw RuntimeLoaderException("Invalid field type");
			}
			offset = (offset + alignment - 1) / alignment * alignment;
			totalAlignment = alignment > totalAlignment ? alignment : totalAlignment;
			type->Fields.push_back({ ftype, offset, len });
			offset += len;
		}
		type->Size = offset;
		type->Alignment = totalAlignment;

		auto ret = type.get();
		_postLoadingTypes.emplace_back(std::move(type));

		assert(_loadingTypes.back() == ret);
		_loadingTypes.pop_back();

		return ret;
	}

	void PostLoadType(std::unique_ptr<RuntimeType> type)
	{
		auto typeTemplate = FindTypeTemplate(type->Args.Assembly, type->Args.Id);

		if (type->Storage == TSM_GLOBAL)
		{
			if (typeTemplate->Interfaces.size() != 0)
			{
				throw RuntimeLoaderException("Global and value type cannot have interfaces");
			}
		}

		if (type->Args.Assembly == "Core" && type->Args.Id == _boxTypeId)
		{
			if (type->Args.Arguments[0]->Storage == TSM_VALUE)
			{
				LoadInterfaces(type.get(), type->Args.Arguments[0], nullptr);
			}
		}
		else if (type->Storage == TSM_INTERFACE || type->Storage == TSM_REFERENCE)
		{
			LoadInterfaces(type.get(), type.get(), typeTemplate);
		}

		type->Initializer = LoadRefFunction({ type.get(), typeTemplate->Generic }, typeTemplate->Initializer);
		type->Finalizer = LoadRefFunction({ type.get(), typeTemplate->Generic }, typeTemplate->Finalizer);
		if (type->Storage != TSM_GLOBAL)
		{
			if (type->Initializer != nullptr)
			{
				throw RuntimeLoaderException("Only global type can have initializer");
			}
		}
		if (type->Storage != TSM_REFERENCE)
		{
			if (type->Finalizer != nullptr)
			{
				throw RuntimeLoaderException("Only reference type can have finalizer");
			}
		}
		_finishedLoadingTypes.emplace_back(std::move(type));
	}

	void PostLoadFunction(std::unique_ptr<RuntimeFunction> func)
	{
		//TODO Optimize loading. Directly find the cloned func/type.
		auto funcTemplate = FindFunctionTemplate(func->Args.Assembly, func->Args.Id);
		for (std::size_t i = 0; i < funcTemplate->Generic.Types.size(); ++i)
		{
			func->ReferencedType.push_back(LoadRefType({ func.get(), funcTemplate->Generic }, i));
		}
		for (std::size_t i = 0; i < funcTemplate->Generic.Functions.size(); ++i)
		{
			func->ReferencedFunction.push_back(LoadRefFunction({ func.get(), funcTemplate->Generic }, i));
		}
		auto assembly = FindAssemblyThrow(func->Args.Assembly);
		for (std::size_t i = 0; i < funcTemplate->Generic.Fields.size(); ++i)
		{
			func->ReferencedFields.push_back(LoadImportConstant(assembly, funcTemplate->Generic.Fields[i]));
		}
		func->ReturnValue = func->ReferencedType[funcTemplate->ReturnValue.TypeId];
		for (std::size_t i = 0; i < funcTemplate->Parameters.size(); ++i)
		{
			func->Parameters.push_back(func->ReferencedType[funcTemplate->Parameters[i].TypeId]);
		}
		auto ptr = func.get();
		_finishedLoadingFunctions.emplace_back(std::move(func));
	}

	void FinalCheckType(RuntimeType* type)
	{
		if (type->Args.Assembly == "Core" && type->Args.Id == _pointerTypeId)
		{
			assert(type->Storage == TSM_VALUE);
			assert(type->Args.Arguments.size() == 1);
			auto elementType = type->Args.Arguments[0];
			assert(elementType->PointerType == nullptr);
			elementType->PointerType = type;
		}
		//TODO check box type?
		if (type->Initializer != nullptr)
		{
			if (type->Initializer->ReturnValue != nullptr ||
				type->Initializer->Parameters.size() != 0)
			{
				throw RuntimeLoaderException("Invalid initializer");
			}
		}
		if (type->Finalizer != nullptr)
		{
			if (type->Finalizer->ReturnValue != nullptr ||
				type->Finalizer->Parameters.size() != 1)
			{
				throw RuntimeLoaderException("Invalid finalizer");
			}
			if (type->Finalizer->Parameters[0] != type)
			{
				throw RuntimeLoaderException("Invalid finalizer");
			}
		}
	}

	void FinalCheckFunction(RuntimeFunction* func)
	{
	}

	void LoadInterfaces(RuntimeType* dest, RuntimeType* src, Type* srcTemplate)
	{
		if (srcTemplate == nullptr)
		{
			srcTemplate = FindTypeTemplate(src->Args.Assembly, src->Args.Id);
		}
		for (auto& i : srcTemplate->Interfaces)
		{
			RuntimeType::InterfaceInfo ii = {};

			auto vtabType = LoadRefType({ src, srcTemplate->Generic }, i.VirtualTableType);
			if (vtabType == nullptr && src->Storage != TSM_INTERFACE)
			{
				throw RuntimeLoaderException("Vtab type not specified for interface");
			}
			if (vtabType != nullptr)
			{
				if (vtabType->Storage != TSM_GLOBAL)
				{
					throw RuntimeLoaderException("Vtab type must be global storage");
				}
				ii.VirtualTable = vtabType;
			}

			auto baseType = LoadRefType({ src, srcTemplate->Generic }, i.InheritedType);
			if (baseType == nullptr)
			{
				throw RuntimeLoaderException("Interface type not specified");
			}
			if (baseType->Storage != TSM_INTERFACE)
			{
				throw RuntimeLoaderException("Interface must be interface storage");
			}
			ii.Type = baseType;

			CheckVirtualTable(ii.Type, ii.VirtualTable);
			dest->Interfaces.push_back(ii);
		}
	}

	void CheckVirtualTable(RuntimeType* baseType, RuntimeType* vtabType)
	{
		if (baseType && baseType->VirtualTableType && vtabType == nullptr)
		{
			throw RuntimeLoaderException("Vtab not matching base type");
		}
		if (vtabType && baseType && baseType->VirtualTableType)
		{
			auto tbase = baseType->VirtualTableType;
			if (tbase->Fields.size() > vtabType->Fields.size())
			{
				throw RuntimeLoaderException("Vtab not matching base type");
			}
			for (std::size_t i = 0; i < tbase->Fields.size(); ++i)
			{
				auto& fbase = tbase->Fields[i];
				auto& fderived = vtabType->Fields[i];
				if (fbase.Type != fderived.Type)
				{
					throw RuntimeLoaderException("Vtab not matching base type");
				}
				assert(fbase.Offset == fderived.Offset);
				assert(fbase.Length == fderived.Length);
			}
		}
	}

private:
	void FindInternalTypeId()
	{
		_pointerTypeId = _boxTypeId = SIZE_MAX;
		if (auto a = FindAssemblyNoThrow("Core"))
		{
			for (auto& e : a->ExportTypes)
			{
				if (e.ExportName == "Core.Pointer")
				{
					if (e.InternalId >= a->Types.size() ||
						!CheckPointerTypeTemplate(&a->Types[e.InternalId]) ||
						_pointerTypeId != SIZE_MAX)
					{
						//This is actually an error, but we don't want to throw in ctor.
						//Let's wait for the type loading to fail.
						return;
					}
					_pointerTypeId = e.InternalId;
				}
				else if (e.ExportName == "Core.Box")
				{
					if (e.InternalId >= a->Types.size() ||
						!CheckBoxTypeTemplate(&a->Types[e.InternalId]) ||
						_boxTypeId != SIZE_MAX)
					{
						return;
					}
					_boxTypeId = e.InternalId;
				}
			}
		}
	}

	bool CheckPointerTypeTemplate(Type* t)
	{
		if (t->Generic.Parameters.size() != 1) return false;
		if (t->GCMode != TSM_VALUE) return false;
		return true;
	}

	bool CheckBoxTypeTemplate(Type* t)
	{
		if (t->Generic.Parameters.size() != 1) return false;
		if (t->GCMode != TSM_REFERENCE) return false;
		return true;
	}

public:
	RuntimeType* LoadPointerType(RuntimeType* t, std::string& err)
	{
		assert(t->PointerType == nullptr);
		LoadingArguments args;
		args.Assembly = "Core";
		args.Id = _pointerTypeId;
		args.Arguments.push_back(t);
		return GetType(args, err);
	}

	//TODO maybe cache result in RuntimeType
	bool IsPointerType(RuntimeType* t)
	{
		return t->Args.Assembly == "Core" && t->Args.Id == _pointerTypeId;
	}

protected:
	virtual void OnTypeLoaded(RuntimeType* type)
	{
	}

	virtual void OnFunctionLoaded(RuntimeFunction* func)
	{
	}

private:
	struct LoadingRefArguments
	{
		const GenericDeclaration& Declaration;
		const LoadingArguments& Arguments;
		RuntimeType* SelfType;

		LoadingRefArguments(RuntimeType* type, const GenericDeclaration& g)
			: Declaration(g), Arguments(type->Args), SelfType(type)
		{
		}

		LoadingRefArguments(RuntimeFunction* func, const GenericDeclaration& g)
			: Declaration(g), Arguments(func->Args), SelfType(nullptr)
		{
		}
	};

	RuntimeType* LoadRefType(LoadingRefArguments lg, std::size_t typeId)
	{
		if (typeId >= lg.Declaration.Types.size())
		{
			throw RuntimeLoaderException("Invalid type reference");
		}
		auto type = lg.Declaration.Types[typeId];
		LoadingArguments la;
	loadClone:
		switch (type.Type)
		{
		case REF_EMPTY:
			return nullptr;
		case REF_CLONE:
			if (type.Index >= lg.Declaration.Types.size())
			{
				throw RuntimeLoaderException("Invalid type reference");
			}
			typeId = type.Index;
			type = lg.Declaration.Types[type.Index];
			goto loadClone;
		case REF_ASSEMBLY:
			la.Assembly = lg.Arguments.Assembly;
			la.Id = type.Index;
			LoadRefTypeArgList(lg, typeId, la);
			return LoadTypeInternal(la);
		case REF_IMPORT:
		{
			auto a = FindAssemblyThrow(lg.Arguments.Assembly);
			if (type.Index >= a->ImportTypes.size())
			{
				throw RuntimeLoaderException("Invalid type reference");
			}
			auto i = a->ImportTypes[type.Index];
			if (!FindExportType(i, la))
			{
				throw RuntimeLoaderException("Import type not found");
			}
			LoadRefTypeArgList(lg, typeId, la);
			if (la.Arguments.size() != i.GenericParameters)
			{
				throw RuntimeLoaderException("Invalid type reference");
			}
			return LoadTypeInternal(la);
		}
		case REF_ARGUMENT:
			if (type.Index >= lg.Arguments.Arguments.size())
			{
				throw RuntimeLoaderException("Invalid type reference");
			}
			return lg.Arguments.Arguments[type.Index];
		case REF_SELF:
			if (lg.SelfType == nullptr)
			{
				throw RuntimeLoaderException("Invalid type reference");
			}
			return lg.SelfType;
		case REF_CLONETYPE:
		default:
			throw RuntimeLoaderException("Invalid type reference");
		}
	}

	void LoadRefTypeArgList(LoadingRefArguments lg, std::size_t index, LoadingArguments& la)
	{
		for (std::size_t i = index + 1; i < lg.Declaration.Types.size(); ++i)
		{
			if (lg.Declaration.Types[i].Type == REF_EMPTY) break; //Use REF_Empty as the end of arg list
			la.Arguments.push_back(LoadRefType(lg, i));
		}
	}

	RuntimeFunction* LoadRefFunction(LoadingRefArguments lg, std::size_t funcId)
	{
		if (funcId >= lg.Declaration.Functions.size())
		{
			throw RuntimeLoaderException("Invalid function reference");
		}
		auto func = lg.Declaration.Functions[funcId];
		LoadingArguments la;
	loadClone:
		switch (func.Type)
		{
		case REF_EMPTY:
			return nullptr;
		case REF_CLONE:
			if (func.Index >= lg.Declaration.Functions.size())
			{
				throw RuntimeLoaderException("Invalid function reference");
			}
			funcId = func.Index;
			func = lg.Declaration.Functions[func.Index];
			goto loadClone;
		case REF_ASSEMBLY:
			la.Assembly = lg.Arguments.Assembly;
			la.Id = func.Index;
			LoadRefFuncArgList(lg, funcId, la);
			return LoadFunctionInternal(la);
		case REF_IMPORT:
		{
			auto a = FindAssemblyThrow(lg.Arguments.Assembly);
			if (func.Index >= a->ImportFunctions.size())
			{
				throw RuntimeLoaderException("Invalid function reference");
			}
			auto i = a->ImportFunctions[func.Index];
			if (!FindExportFunction(i, la))
			{
				throw RuntimeLoaderException("Import function not found");
			}
			LoadRefFuncArgList(lg, funcId, la);
			if (la.Arguments.size() != i.GenericParameters)
			{
				throw RuntimeLoaderException("Invalid function reference");
			}
			return LoadFunctionInternal(la);
		}
		case REF_CLONETYPE:
			return nullptr;
		case REF_ARGUMENT:
		default:
			throw RuntimeLoaderException("Invalid function reference");
		}
	}

	void LoadRefFuncArgList(LoadingRefArguments lg, std::size_t index, LoadingArguments& la)
	{
		for (std::size_t i = index + 1; i < lg.Declaration.Functions.size(); ++i)
		{
			if (lg.Declaration.Functions[i].Type == REF_EMPTY) break;
			if (lg.Declaration.Functions[i].Type != REF_CLONETYPE)
			{
				throw RuntimeLoaderException("Invalid generic function argument");
			}
			la.Arguments.push_back(LoadRefType(lg, lg.Declaration.Functions[i].Index));
		}
	}

protected:
	Assembly* FindAssemblyNoThrow(const std::string& name)
	{
		for (auto& a : _assemblies.Assemblies)
		{
			if (a.AssemblyName == name)
			{
				return &a;
			}
		}
		return nullptr;
	}

	Assembly* FindAssemblyThrow(const std::string& name)
	{
		auto ret = FindAssemblyNoThrow(name);
		if (ret == nullptr)
		{
			throw RuntimeLoaderException("Referenced assembly not found");
		}
		return ret;
	}

	std::size_t FindNativeIdNoThrow(const std::vector<AssemblyExport>& list,
		const std::string name)
	{
		for (std::size_t i = 0; i < list.size(); ++i)
		{
			if (list[i].ExportName == name)
			{
				return list[i].InternalId;
			}
		}
		return SIZE_MAX;
	}

	std::size_t FindNativeIdThrow(const std::vector<AssemblyExport>& list,
		const std::string name)
	{
		auto ret = FindNativeIdNoThrow(list, name);
		if (ret == SIZE_MAX)
		{
			throw RuntimeLoaderException("Native object not found");
		}
		return ret;
	}

	Type* FindTypeTemplate(const std::string& assembly, std::size_t id)
	{
		auto a = FindAssemblyThrow(assembly);
		if (id >= a->Types.size())
		{
			throw RuntimeLoaderException("Invalid type reference");
		}
		return &a->Types[id];
	}

	Function* FindFunctionTemplate(const std::string& assembly, std::size_t id)
	{
		auto a = FindAssemblyThrow(assembly);
		if (id >= a->Functions.size())
		{
			throw RuntimeLoaderException("Invalid function reference");
		}
		return &a->Functions[id];
	}

private:
	std::shared_ptr<RuntimeFunctionCode> GetCode(const std::string& a, std::size_t id)
	{
		for (auto& c : _codeStorage.Data)
		{
			if (c->AssemblyName == a && c->Id == id)
			{
				return c;
			}
		}
		auto f = FindFunctionTemplate(a, id);
		if (f->Instruction.size() == 0 && f->ConstantData.size() == 0 &&
			f->ConstantTable.size() == 0)
		{
			return nullptr;
		}
		auto ret = std::make_shared<RuntimeFunctionCode>();
		ret->AssemblyName = a;
		ret->Id = id;
		ret->Instruction = f->Instruction;
		ret->ConstantData = f->ConstantData;
		ret->ConstantTable = f->ConstantTable;
		ret->LocalVariables = f->Locals;

		//Append some nop at the end
		for (int i = 0; i < 16; ++i)
		{
			ret->Instruction.push_back(OP_NOP);
		}

		//Process import constant
		auto assembly = FindAssemblyThrow(a);
		for (auto& k : ret->ConstantTable)
		{
			if (k.Length == 0)
			{
				auto kid = k.Offset;
				auto value = LoadImportConstant(assembly, kid);
				auto offset = ret->ConstantData.size();
				auto pValue = (unsigned char*)&value;
				ret->ConstantData.insert(ret->ConstantData.end(), pValue, pValue + 4);
				k.Length = 4;
				k.Offset = offset;
			}
		}

		_codeStorage.Data.push_back(ret);
		return std::move(ret);
	}

	void AddLoadedType(std::unique_ptr<RuntimeType> t)
	{
		auto id = t->TypeId;
		if (id < _loadedTypes.size())
		{
			assert(_loadedTypes[id] == nullptr);
			_loadedTypes[id] = std::move(t);
		}
		else
		{
			while (id > _loadedTypes.size())
			{
				_loadedTypes.push_back(nullptr);
			}
			_loadedTypes.emplace_back(std::move(t));
		}
	}

	void AddLoadedFunction(std::unique_ptr<RuntimeFunction> f)
	{
		auto id = f->FunctionId;
		if (id < _loadedFunctions.size())
		{
			assert(_loadedFunctions[id] == nullptr);
			_loadedFunctions[id] = std::move(f);
		}
		else
		{
			while (id > _loadedFunctions.size())
			{
				_loadedFunctions.push_back(nullptr);
			}
			_loadedFunctions.emplace_back(std::move(f));
		}
	}

protected:
	//We don't expect loader to run very often. A simple spinlock
	//should be enough.
	Spinlock _loaderLock;

private:
	AssemblyList _assemblies;
	std::size_t _ptrSize, _itabPtrSize;
	std::size_t _loadingLimit;

	std::vector<std::unique_ptr<RuntimeType>> _loadedTypes;
	std::vector<std::unique_ptr<RuntimeFunction>> _loadedFunctions;
	RuntimeFunctionCodeStorage _codeStorage;

	std::vector<RuntimeType*> _loadingTypes;

	//Loading queues. We need to keep order.
	std::deque<std::unique_ptr<RuntimeType>> _loadingRefTypes;
	std::deque<std::unique_ptr<RuntimeType>> _postLoadingTypes;
	std::deque<std::unique_ptr<RuntimeFunction>> _loadingFunctions;
	std::deque<std::unique_ptr<RuntimeType>> _finishedLoadingTypes;
	std::deque<std::unique_ptr<RuntimeFunction>> _finishedLoadingFunctions;

	std::uint32_t _nextFunctionId = 1, _nextTypeId = 1;
	std::size_t _pointerTypeId, _boxTypeId;
};

inline std::size_t RuntimeType::GetStorageSize()
{
	return Storage == TSM_REFERENCE || Storage == TSM_INTERFACE ?
		Parent->GetPointerSize() : Size;
}

inline std::size_t RuntimeType::GetStorageAlignment()
{
	return Storage == TSM_REFERENCE || Storage == TSM_INTERFACE ?
		Parent->GetPointerSize() : Alignment;
}
