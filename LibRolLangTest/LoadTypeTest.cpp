#include "stdafx.h"

namespace LibRolLangTest
{
	using Builder = TestAssemblyListBuilder;
	using TypeReference = Builder::TypeReference;
	using namespace RuntimeLoaderHelper;

	TEST_CLASS(LoadTypeTest)
	{
	private:
		static void SetupEmptyType(Builder& builder)
		{
			builder.BeginType(TSM_VALUE, "Test.SingleType");
			builder.Link(true, false);
			builder.EndType();
		}

		static void CheckEmptyType(RuntimeLoader* loader)
		{
			auto t = LoadType(loader, "Test", "Test.SingleType", ERR_L_SUCCESS);
			CheckValueTypeBasic(loader, t);
			CheckValueTypeSize(t, 0, 1);
			CheckFieldOffset(t, {});
		}

		static void SetupNativeType(Builder& builder, TypeReference* t1, TypeReference* t4)
		{
			*t1 = builder.BeginType(TSM_VALUE, "Test.Native1");
			builder.Link(true, true);
			builder.EndType();

			*t4 = builder.BeginType(TSM_VALUE, "Test.Native4");
			builder.Link(true, true);
			builder.EndType();
		}

		static void CheckNativeType(RuntimeLoader* loader)
		{
			auto t1 = LoadNativeType(loader, "Test", "Test.Native1", 1);
			CheckValueTypeBasic(loader, t1);
			CheckValueTypeSize(t1, 1, 1);
			CheckFieldOffset(t1, {});

			auto t4 = LoadNativeType(loader, "Test", "Test.Native4", 4);
			CheckValueTypeBasic(loader, t4);
			CheckValueTypeSize(t4, 4, 4);
			CheckFieldOffset(t4, {});
		}

		static void SetupValueType(Builder& builder, TypeReference t1, TypeReference t4)
		{
			auto a = builder.BeginType(TSM_VALUE, "Test.ValueTypeA");
			builder.Link(false, false);
			builder.AddField(t1);
			builder.AddField(t1);
			builder.AddField(t4);
			builder.AddField(t4);
			builder.AddField(t1);
			builder.EndType();

			builder.BeginType(TSM_VALUE, "Test.ValueTypeB");
			builder.Link(true, false);
			builder.AddField(a);
			builder.AddField(t1);
			builder.AddField(t4);
			builder.EndType();
		}

		static void CheckValueType(RuntimeLoader* loader)
		{
			auto b = LoadType(loader, "Test", "Test.ValueTypeB", ERR_L_SUCCESS);
			auto a = b->Fields[0].Type;

			CheckValueTypeBasic(loader, a);
			CheckValueTypeSize(a, 13, 4);
			CheckFieldOffset(a, { 0, 1, 4, 8, 12 });

			CheckValueTypeBasic(loader, b);
			CheckValueTypeSize(b, 20, 4);
			CheckFieldOffset(b, { 0, 13, 16 });
		}

		static void SetupReferenceType(Builder& builder, TypeReference t1, TypeReference t4)
		{
			auto a = builder.BeginType(TSM_REFERENCE, "Test.RefTypeA");
			builder.Link(false, false);
			builder.AddField(t1);
			builder.AddField(t4);
			builder.EndType();

			builder.BeginType(TSM_REFERENCE, "Test.RefTypeB");
			builder.Link(true, false);
			builder.AddField(t4);
			builder.AddField(a);
			builder.AddField(t4);
			builder.EndType();
		}

		static void CheckReferenceType(RuntimeLoader* loader)
		{
			auto b = LoadType(loader, "Test", "Test.RefTypeB", ERR_L_SUCCESS);
			auto a = b->Fields[1].Type;

			CheckReferenceTypeBasic(loader, a);
			CheckValueTypeSize(a, 8, 4);
			CheckFieldOffset(a, { 0, 4 });

			CheckReferenceTypeBasic(loader, b);
			CheckValueTypeSize(b, sizeof(void*) * 2 + 4, sizeof(void*));
			CheckFieldOffset(b, { 0, sizeof(void*), sizeof(void*) * 2 });
		}

		static void SetupGlobalType(Builder& builder, TypeReference t1, TypeReference t4)
		{
			auto a = builder.BeginType(TSM_VALUE, "Test.ValueTypeG1");
			builder.Link(false, false);
			builder.AddField(t4);
			builder.AddField(t4);
			builder.EndType();

			builder.BeginType(TSM_GLOBAL, "Test.GlobalType");
			builder.Link(true, false);
			builder.AddField(a);
			builder.AddField(t4);
			builder.EndType();
		}

		static void CheckGlobalType(RuntimeLoader* loader)
		{
			auto b = LoadType(loader, "Test", "Test.GlobalType", ERR_L_SUCCESS);

			CheckGlobalTypeBasic(loader, b);
			CheckValueTypeSize(b, 12, 4);
			CheckFieldOffset(b, { 0, 8 });
		}

		static void CheckValueTypeFailExportName(RuntimeLoader* loader)
		{
			LoadType(loader, "Test", "Test.ValueTypeA", ERR_L_LINK);
			LoadType(loader, "Test", "Test.ValueTypeC", ERR_L_LINK);
			CheckValueType(loader);
		}

		static void SetupValueTypeFailTypeReference(Builder& builder)
		{
			TypeReference r1, r2;
			r1.Type = Builder::TR_TEMP;
			r1.Id = 0;
			r2.Type = Builder::TR_TEMP;
			r2.Id = 100;
			builder.BeginType(TSM_VALUE, "Test.ValueTypeC");
			builder.Link(true, false);
			builder.AddField(r1);
			builder.AddField(r2);
			builder.EndType();
		}

		static void CheckValueTypeFailTypeReference(RuntimeLoader* loader)
		{
			LoadType(loader, "Test", "Test.ValueTypeC", ERR_L_PROGRAM);
			CheckValueType(loader);
		}

		static void SetupTemplateType(Builder& builder, TypeReference t1, TypeReference t4)
		{
			auto tt = builder.BeginType(TSM_VALUE, "Test.TemplateType");
			auto g1 = builder.AddGenericParameter();
			auto g2 = builder.AddGenericParameter();
			builder.Link(false, false);
			builder.AddField(g1);
			builder.AddField(g2);
			builder.EndType();

			auto tt11 = builder.MakeType(tt, { t1, t1 });
			auto tt12 = builder.MakeType(tt, { t1, t4 });

			builder.BeginType(TSM_VALUE, "Test.TemplateTestType1");
			builder.Link(true, false);
			builder.AddField(tt11);
			builder.AddField(tt12);
			builder.EndType();

			builder.BeginType(TSM_VALUE, "Test.TemplateTestType2");
			auto g3 = builder.AddGenericParameter();
			auto tt2 = builder.MakeType(tt, { t4, g3 });
			builder.Link(true, false);
			builder.AddField(tt2);
			builder.EndType();
		}

		static void CheckTemplateType(RuntimeLoader* loader)
		{
			auto t1 = LoadType(loader, "Test", "Test.TemplateTestType1", ERR_L_SUCCESS);
			auto t11 = t1->Fields[0].Type;
			auto t12 = t1->Fields[1].Type;

			CheckValueTypeBasic(loader, t11);
			CheckValueTypeSize(t11, 2, 1);
			CheckFieldOffset(t11, { 0, 1 });

			CheckValueTypeBasic(loader, t12);
			CheckValueTypeSize(t12, 8, 4);
			CheckFieldOffset(t12, { 0, 4 });

			CheckValueTypeBasic(loader, t1);
			CheckValueTypeSize(t1, 12, 4);
			CheckFieldOffset(t1, { 0, 4 });

			auto t2 = LoadType(loader, "Test", "Test.TemplateTestType2",
				{ t11->Fields[0].Type }, ERR_L_SUCCESS);
			auto t21 = t2->Fields[0].Type;

			CheckValueTypeBasic(loader, t21);
			CheckValueTypeSize(t21, 5, 4);
			CheckFieldOffset(t21, { 0, 4 });

			CheckValueTypeBasic(loader, t2);
			CheckValueTypeSize(t2, 5, 4);
			CheckFieldOffset(t2, { 0 });
		}

		static void SetupCyclicType(Builder& builder)
		{
			auto t1b = builder.ForwardDeclareType();
			auto t1a = builder.BeginType(TSM_VALUE, "Test.CycType1A");
			builder.Link(true, false);
			builder.AddField(t1b);
			builder.EndType();

			builder.BeginType(TSM_VALUE, "Test.CycType1B", t1b);
			builder.Link(false, false);
			builder.AddField(t1a);
			builder.EndType();

			auto t2b = builder.ForwardDeclareType();
			auto t2a = builder.BeginType(TSM_VALUE, "Test.CycType2A");
			builder.Link(true, false);
			builder.AddField(t2b);
			builder.EndType();

			builder.BeginType(TSM_REFERENCE, "Test.CycType2B", t2b);
			builder.Link(false, false);
			builder.AddField(t2a);
			builder.EndType();

			auto t3 = builder.BeginType(TSM_REFERENCE, "Test.CycType3A");
			builder.Link(true, false);
			builder.AddField(t3);
			builder.EndType();

			builder.BeginType(TSM_REFERENCE, "Test.CycType4");
			builder.Link(true, false);
			builder.AddField(builder.SelfType());
			builder.EndType();
		}

		static void CheckCyclicType(RuntimeLoader* loader)
		{
			LoadType(loader, "Test", "Test.CycType1A", ERR_L_CIRCULAR);

			auto t2a = LoadType(loader, "Test", "Test.CycType2A", ERR_L_SUCCESS);
			auto t2b = t2a->Fields[0].Type;
			CheckValueTypeBasic(loader, t2a);
			CheckValueTypeSize(t2a, sizeof(void*), sizeof(void*));
			CheckReferenceTypeBasic(loader, t2b);
			CheckValueTypeSize(t2b, t2a->Size, t2a->Alignment);

			auto t3 = LoadType(loader, "Test", "Test.CycType3A", ERR_L_SUCCESS);
			CheckReferenceTypeBasic(loader, t3);
			CheckValueTypeSize(t3, sizeof(void*), sizeof(void*));

			auto t4 = LoadType(loader, "Test", "Test.CycType4", ERR_L_SUCCESS);
			CheckReferenceTypeBasic(loader, t4);
			Assert::AreEqual((std::uintptr_t)t4, (std::uintptr_t)t4->Fields[0].Type);
		}

	public:
		TEST_METHOD(LoadEmptyType)
		{
			Builder builder;

			builder.BeginAssembly("Test");
			SetupEmptyType(builder);
			builder.EndAssembly();
			
			RuntimeLoader loader(builder.Build());
			CheckEmptyType(&loader);
			CheckEmptyType(&loader);
		}

		TEST_METHOD(LoadValueType)
		{
			Builder builder;
			TypeReference t1, t4;

			builder.BeginAssembly("Test");
			SetupNativeType(builder, &t1, &t4);
			SetupValueType(builder, t1, t4);
			builder.EndAssembly();

			RuntimeLoader loader(builder.Build());
			CheckNativeType(&loader);
			CheckValueType(&loader);
			CheckNativeType(&loader);
			CheckValueType(&loader);
		}

		TEST_METHOD(LoadReferenceType)
		{
			Builder builder;
			TypeReference t1, t4;

			builder.BeginAssembly("Test");
			SetupNativeType(builder, &t1, &t4);
			SetupReferenceType(builder, t1, t4);
			builder.EndAssembly();

			RuntimeLoader loader(builder.Build());
			CheckNativeType(&loader);
			CheckReferenceType(&loader);
			CheckNativeType(&loader);
			CheckReferenceType(&loader);
		}

		TEST_METHOD(LoadGlobalType)
		{
			Builder builder;
			TypeReference t1, t4;

			builder.BeginAssembly("Test");
			SetupNativeType(builder, &t1, &t4);
			SetupGlobalType(builder, t1, t4);
			builder.EndAssembly();

			RuntimeLoader loader(builder.Build());
			CheckNativeType(&loader);
			CheckGlobalType(&loader);
			CheckNativeType(&loader);
			CheckGlobalType(&loader);
		}

		TEST_METHOD(LoadFailExportName)
		{
			Builder builder;
			TypeReference t1, t4;

			builder.BeginAssembly("Test");
			SetupNativeType(builder, &t1, &t4);
			SetupValueType(builder, t1, t4);
			builder.EndAssembly();

			RuntimeLoader loader(builder.Build());
			CheckNativeType(&loader);
			CheckValueTypeFailExportName(&loader);
		}

		TEST_METHOD(LoadFailTypeReference)
		{
			Builder builder;
			TypeReference t1, t4;

			builder.BeginAssembly("Test");
			SetupNativeType(builder, &t1, &t4);
			SetupValueType(builder, t1, t4);
			SetupValueTypeFailTypeReference(builder);
			builder.EndAssembly();

			RuntimeLoader loader(builder.Build());
			CheckNativeType(&loader);
			CheckValueTypeFailTypeReference(&loader);
		}

		TEST_METHOD(LoadTemplateType)
		{
			Builder builder;
			TypeReference t1, t4;

			builder.BeginAssembly("Test");
			SetupNativeType(builder, &t1, &t4);
			SetupTemplateType(builder, t1, t4);
			builder.EndAssembly();

			RuntimeLoader loader(builder.Build());
			CheckNativeType(&loader);
			CheckTemplateType(&loader);
		}

		TEST_METHOD(LoadCyclicReferencedType)
		{
			Builder builder;
			TypeReference t1, t4;

			builder.BeginAssembly("Test");
			SetupCyclicType(builder);
			builder.EndAssembly();

			RuntimeLoader loader(builder.Build());
			CheckCyclicType(&loader);
		}
	};
}
