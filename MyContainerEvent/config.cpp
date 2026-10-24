class CfgPatches
{
	class MyContainerEvent_Scripts
	{
		units[]=
		{
			"CONTAINER_Chest"
		};
		weapons[]={};
		requiredVersion=0.1;
		requiredAddons[]=
		{
			"DZ_Data",
			"DZ_Scripts",
			"DZ_Gear_Camping"
		};
	};
};
class CfgMods
{
	class MyContainerEvent
	{
		dir="MyContainerEvent";
		picture="";
		action="";
		hideName=1;
		hidePicture=1;
		name="Container Event Mod";
		credits="SafeDoNordeste Thiago Santana";
		author="SafeDoNordeste Thiago Santana";
		authorID="0";
		version="1.0";
		extra=0;
		type="mod";
		dependencies[]=
		{
			"World",
			"Mission"
		};
		class defs
		{
			class worldScriptModule
			{
				value="";
				files[]=
				{
					"MyContainerEvent/scripts/4_World"
				};
			};
			class missionScriptModule
			{
				value="";
				files[]=
				{
					"MyContainerEvent/scripts/5_Mission"
				};
			};
		};
	};
};
class CfgVehicles
{
	class HouseNoDestruct;
	class Inventory_Base;
	class SeaChest;
	class Land_ContainerLocked_Blue_DE;
	class Land_ContainerLocked_Yellow_DE;
	class Land_ContainerLocked_Orange_DE;
	class Land_ContainerLocked_Red_DE;

	class SDN_EventContainer_Blue: Land_ContainerLocked_Blue_DE
	{
		scope=2;
		hiddenSelections[]=
		{
			"Camo"
		};
		hiddenSelectionsTextures[]=
		{
			"MyContainerEvent\data\SDN_retexture_containerlocked.paa"
		};
	};
	class SDN_EventContainer_Yellow: Land_ContainerLocked_Yellow_DE
	{
		scope=2;
		hiddenSelections[]=
		{
			"Camo"
		};
		hiddenSelectionsTextures[]=
		{
			"MyContainerEvent\data\SDN_retexture_containerlocked.paa"
		};
	};
	class SDN_EventContainer_Orange: Land_ContainerLocked_Orange_DE
	{
		scope=2;
		hiddenSelections[]=
		{
			"Camo"
		};
		hiddenSelectionsTextures[]=
		{
			"MyContainerEvent\data\SDN_retexture_containerlocked.paa"
		};
	};
	class SDN_EventContainer_Red: Land_ContainerLocked_Red_DE
	{
		scope=2;
		hiddenSelections[]=
		{
			"Camo"
		};
		hiddenSelectionsTextures[]=
		{
			"MyContainerEvent\data\SDN_retexture_containerlocked.paa"
		};
	};

	class BaseBuildingBase: Inventory_Base
	{
	};
	class CONTAINER_Chest: SeaChest
	{
		scope=2;
		displayName="Reward Chest";
		descriptionShort="Chest containing rewards CONTAINER!";
		weight=10000;
		itemBehaviour=0;
		itemSize[]={10,5};
		canBeDigged=0;
		hiddenSelections[]=
		{
			"camoGround"
		};
		hiddenSelectionsTextures[]=
		{
			"MyContainerEvent\data\sea_chest_evente_container.paa"
		};
		attachments[]={};
		class DamageSystem
		{
			class GlobalArmor
			{
				class FragGrenade
				{
					class Health
					{
						damage=0;
					};
					class Blood
					{
						damage=0;
					};
					class Shock
					{
						damage=0;
					};
				};
			};
		};
		class Cargo
		{
			itemsCargoSize[]={10,50};
			openable=0;
			allowOwnedCargoManipulation=1;
		};
	};
};
class CfgSoundSets
{
	class notification_SoundSet
	{
		soundShaders[]=
		{
			"notification_SoundShader"
		};
		volumeFactor=1;
	};
};
class CfgSoundShaders
{
	class notification_SoundShader_Base
	{
		samples[]={};
		frequency=1;
		range=35;
		volume=0.75;
	};
	class notification_SoundShader: notification_SoundShader_Base
	{
		samples[]=
		{
			
			{
				"MyContainerEvent\Sounds\Sound01",
				1
			}
		};
	};
};
