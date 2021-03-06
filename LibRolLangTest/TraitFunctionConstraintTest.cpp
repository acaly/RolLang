#include "stdafx.h"

namespace LibRolLangTest
{
	using namespace RuntimeLoaderHelper;
	using Builder = TestAssemblyListBuilder;

	TEST_CLASS(TraitFunctionConstraintTest)
	{
		TEST_METHOD(TypesAreEqual)
		{
			Builder b;
			{
				b.BeginAssembly("Core");
				auto tf = b.BeginFunction("Core.TargetFunction");
				auto fg1 = b.AddGenericParameter();
				auto fg2 = b.AddGenericParameter();
				b.AddConstraint(fg1, { fg2 }, CONSTRAINT_SAME, 0);
				b.Signature({}, { fg1, fg2 });
				b.EndFunction();
				auto tt = b.BeginType(TSM_VALUE, "Core.TargetType");
				auto ttf = b.MakeFunction(tf, { b.AddAdditionalGenericParameter(0), b.AddAdditionalGenericParameter(1) });
				b.AddMemberFunction("Func", ttf);
				b.EndType();
				
				auto vt1 = b.BeginType(TSM_VALUE, "Core.ValueType1");
				b.EndType();
				auto vt2 = b.BeginType(TSM_VALUE, "Core.ValueType2");
				b.EndType();

				auto tr1 = b.BeginTrait("Core.Trait1");
				b.AddTraitFunction({}, { vt1, vt1 }, "Func", "Func");
				b.EndTrait();
				auto tr2 = b.BeginTrait("Core.Trait2");
				b.AddTraitFunction({}, { vt1, vt2 }, "Func", "Func");
				b.EndTrait();

				b.BeginType(TSM_VALUE, "Core.TestType1");
				b.Link(true, false);
				b.AddConstraint(tt, {}, CONSTRAINT_TRAIT_ASSEMBLY, tr1.Id);
				b.EndType();
				b.BeginType(TSM_VALUE, "Core.TestType2");
				b.Link(true, false);
				b.AddConstraint(tt, {}, CONSTRAINT_TRAIT_ASSEMBLY, tr2.Id);
				b.EndType();
				b.EndAssembly();
			}
			RuntimeLoader l(b.Build());
			{
				LoadType(&l, "Core", "Core.TestType1", ERR_L_SUCCESS);
				LoadType(&l, "Core", "Core.TestType2", ERR_L_GENERIC);
			}
		}

		TEST_METHOD(ConstraintImportType)
		{
			//void Func<T>(T x, T1.Sub y) requires ParentType<T> as T1
			//struct ParentType<T> { alias Sub = T; }
			//Func(vt1, vt1) -> success
			//Func(vt1, vt2) -> failure
			Builder b;
			{
				b.BeginAssembly("Core");
				auto pt = b.BeginType(TSM_VALUE, "Core.ParentType");
				{
					auto g = b.AddGenericParameter();
					b.AddSubType("Sub", g);
				}
				b.EndType();
				auto tf = b.BeginFunction("Core.TargetFunction");
				{
					auto tfg = b.AddGenericParameter();
					auto tfct = b.MakeType(pt, { tfg });
					b.AddConstraint(b.TryType(tfct), {}, CONSTRAINT_EXIST, 0, "constraint");
					auto arg2 = b.MakeSubtype(b.ConstraintImportType("constraint/.target"), "Sub", {});
					b.Signature({}, { tfg, arg2 });
				}
				b.EndFunction();
				auto tt = b.BeginType(TSM_VALUE, "Core.TargetType");
				auto ttf = b.MakeFunction(tf, { b.AddAdditionalGenericParameter(0) });
				b.AddMemberFunction("Func", ttf);
				b.EndType();

				auto vt1 = b.BeginType(TSM_VALUE, "Core.ValueType1");
				b.EndType();
				auto vt2 = b.BeginType(TSM_VALUE, "Core.ValueType2");
				b.EndType();

				auto tr1 = b.BeginTrait("Core.Trait1");
				b.AddTraitFunction({}, { vt1, vt1 }, "Func", "Func");
				b.EndTrait();
				auto tr2 = b.BeginTrait("Core.Trait2");
				b.AddTraitFunction({}, { vt1, vt2 }, "Func", "Func");
				b.EndTrait();

				b.BeginType(TSM_VALUE, "Core.TestType1");
				b.Link(true, false);
				b.AddConstraint(tt, {}, CONSTRAINT_TRAIT_ASSEMBLY, tr1.Id);
				b.EndType();
				b.BeginType(TSM_VALUE, "Core.TestType2");
				b.Link(true, false);
				b.AddConstraint(tt, {}, CONSTRAINT_TRAIT_ASSEMBLY, tr2.Id);
				b.EndType();
				b.EndAssembly();
			}
			RuntimeLoader l(b.Build());
			{
				LoadType(&l, "Core", "Core.TestType1", ERR_L_SUCCESS);
				LoadType(&l, "Core", "Core.TestType2", ERR_L_GENERIC);
			}
		}
	};
}
