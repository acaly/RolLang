#include "stdafx.h"

namespace LibRolLangTest
{
	using namespace RuntimeLoaderHelper;
	using Builder = TestAssemblyListBuilder;

	TEST_CLASS(ConstrainMemberExportTest)
	{
		TEST_METHOD(ExportSubType)
		{
			Builder b;
			{
				b.BeginAssembly("Core");
				auto tr = b.BeginTrait("Core.Trait");
				auto subtype = b.MakeSubtype(b.SelfType(), "A", {});
				b.AddConstrain(b.TryType(subtype), {}, CONSTRAIN_EXIST, 0);
				b.AddTraitType(subtype, "a");
				b.EndTrait();
				auto vt = b.BeginType(TSM_VALUE, "Core.ValueType");
				b.Link(true, false);
				b.EndType();
				auto tt = b.BeginType(TSM_VALUE, "Core.TargetType");
				b.AddSubType("A", vt);
				b.EndType();
				b.BeginType(TSM_VALUE, "Core.TestType");
				b.Link(true, false);
				b.AddConstrain(tt, {}, CONSTRAIN_TRAIT_ASSEMBLY, tr.Id, "subtype");
				b.AddField(b.ConstrainImportType("subtype/a"));
				b.EndType();
				b.EndAssembly();
			}
			RuntimeLoader l(b.Build());
			{
				auto vt = LoadType(&l, "Core", "Core.ValueType", false);
				CheckValueTypeBasic(&l, vt);
				auto tt = LoadType(&l, "Core", "Core.TestType", false);
				CheckValueTypeBasic(&l, vt);
				CheckFieldOffset(tt, { 0 });
				Assert::AreEqual((std::uintptr_t)vt, (std::uintptr_t)tt->Fields[0].Type);
			}
		}
	};
}
