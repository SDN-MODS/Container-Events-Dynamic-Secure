
class SDN_SceneObjectTemplate
{
    string ClassName;
    vector OffsetPos;
    vector OffsetOri;

    void SDN_SceneObjectTemplate(string cls, vector pos, vector ori)
    {
        ClassName = cls;
        OffsetPos = pos;
        OffsetOri = ori;
    }
}

/*
    ActiveEventInstance.c
*/

class ActiveEventInstance
{
    int EventID;
    vector Position;
    string LocationName;

    EntityAI Container;
    ItemBase LootCrate;

    // Controle Visual (Fumaça)
    ItemBase VisualEffect;
    float SmokeTimer; // Cronômetro para trocar a granada antes que apague
    string CurrentSmokeColor; // Para saber se precisamos trocar a cor

    int InitialLootCount;

    string ContainerType;
    string KeyType;
    int Tier;

    ref array<EntityAI> Zombies;
    ref array<Object> SceneObjects;
    bool IsActive;
    bool IsCompleted;
    float EventTimer;
    float LifeTime;
    float DebugTimer;

    void ActiveEventInstance()
    {
        Zombies = new array<EntityAI>;
        SceneObjects = new array<Object>;
        IsActive = false;
        IsCompleted = false;
        EventTimer = 0;
        LifeTime = 0;
        KeyType = "";
        DebugTimer = 0;
        InitialLootCount = 0;

        SmokeTimer = 0;
        CurrentSmokeColor = "";
    }

    void Cleanup()
    {
        if (VisualEffect) GetGame().ObjectDelete(VisualEffect);
        if (LootCrate) GetGame().ObjectDelete(LootCrate);
        if (Container) GetGame().ObjectDelete(Container);

        if (Zombies)
        {
            foreach (EntityAI zmb : Zombies)
            {
                if (zmb && zmb.IsAlive()) GetGame().ObjectDelete(zmb);
            }
        }
        Zombies.Clear();
        if (SceneObjects)
        {
            foreach (Object sc : SceneObjects)
            {
                if (sc) GetGame().ObjectDelete(sc);
            }
        }
        SceneObjects.Clear();
    }
}/*
    Arquivo: SDN_ContainerEventManager.c
    Caminho (relativo ao mod): scripts/4_World/SDN_ContainerEventManager.c
    Versão: SDN_MODS Folder + Discord Webhook (FIXED Header) + Custom Titles + Expanded Formatting
*/

enum SDN_NotificationRPC
{
    SDN_EVENT_MSG = 8410225
}

class SDN_ContainerEventManager
{
    protected ref SDN_ContainerEventConfig m_Config;
    protected ref array<ref ActiveEventInstance> m_ActiveEvents;

    protected float m_UpdateTimer;
    protected float m_SpawnTimer;
    protected float m_CurrentTargetTime;
    protected float m_SaveTimer;

    // --- SAFE START ---
    protected bool m_IsInitialized;
    protected float m_InitDelayTimer;

    protected int m_EventsSpawnedInWave;
    protected string m_LastSpawnedLocName;

    protected ref array<string> m_HistoryLocations;
    protected ref array<string> m_CurrentWaveLocations;
    protected ref array<ref SDN_SceneObjectTemplate> m_SceneTemplate;
    protected ref array<vector> m_GhostLocations;

    // [SDN] Nova estrutura de pastas padronizada
    const string MOD_FOLDER_ROOT = "$profile:SDN_MODS";
    const string CONFIG_ROOT = "$profile:SDN_MODS/ContainerEvent";
    const string CONFIG_PATH = "$profile:SDN_MODS/ContainerEvent/EventConfig.json";
    const string STATE_PATH = "$profile:SDN_MODS/ContainerEvent/EventState.json";
    const string LOG_PATH = "$profile:SDN_MODS/ContainerEvent/EventLog.txt";

    const string ICON_PATH = "MyContainerEvent/data/gui/icon_containe_event.edds";

    void SDN_ContainerEventManager()
    {
        m_ActiveEvents = new array<ref ActiveEventInstance>;
        m_HistoryLocations = new array<string>;
        m_CurrentWaveLocations = new array<string>;
        m_GhostLocations = new array<vector>;

        m_SpawnTimer = 0;
        m_EventsSpawnedInWave = 0;
        m_LastSpawnedLocName = "";
        m_SaveTimer = 0;

        m_IsInitialized = false;
        m_InitDelayTimer = 0;

        if (!FileExist(MOD_FOLDER_ROOT))
        {
            MakeDirectory(MOD_FOLDER_ROOT);
        }

        if (!FileExist(CONFIG_ROOT))
        {
            MakeDirectory(CONFIG_ROOT);
        }

        LoadConfig();
        InitSceneTemplate();
        Log("Mod loaded. Waiting for server stabilization (Safe Start)...");
    }

    void RunDelayedInit()
    {
        Log("Server stabilized. Starting event and ghost scan...");
        LoadState();
        HandleCrashGhosts();
        m_IsInitialized = true;
    }

    // =========================================================================
    // UTILITÁRIOS BÁSICOS
    // =========================================================================

    void Log(string msg)
    {
        string timestamp = GetDate();
        string logLine = string.Format("[%1] %2", timestamp, msg);
        Print("[ContainerEvent] " + msg);

        FileHandle file = OpenFile(LOG_PATH, FileMode.APPEND);

        if (file != 0)
        {
            FPrintln(file, logLine);
            CloseFile(file);
        }
    }

    string GetDate()
    {
        int year, month, day, hour, minute, second;
        GetYearMonthDay(year, month, day);
        GetHourMinuteSecond(hour, minute, second);
        return string.Format("%1-%2-%3 %4:%5:%6", year.ToStringLen(4), month.ToStringLen(2), day.ToStringLen(2), hour.ToStringLen(2), minute.ToStringLen(2), second.ToStringLen(2));
    }

    // =========================================================================
    // SEGURANÇA E IDs (SetIDSafe)
    // =========================================================================

    void SetIDSafe(Object obj, int id)
    {
        if (!obj)
        {
            return;
        }

        ShippingContainerKeys_ColorBase key = ShippingContainerKeys_ColorBase.Cast(obj);

        if (key)
        {
            key.SetSDNEventID(id);
            return;
        }

        Land_ContainerLocked_Blue_DE blue = Land_ContainerLocked_Blue_DE.Cast(obj);

        if (blue)
        {
            blue.SetSDNEventID(id);
            return;
        }

        Land_ContainerLocked_Yellow_DE yellow = Land_ContainerLocked_Yellow_DE.Cast(obj);

        if (yellow)
        {
            yellow.SetSDNEventID(id);
            return;
        }

        Land_ContainerLocked_Orange_DE orange = Land_ContainerLocked_Orange_DE.Cast(obj);

        if (orange)
        {
            orange.SetSDNEventID(id);
            return;
        }

        Land_ContainerLocked_Red_DE red = Land_ContainerLocked_Red_DE.Cast(obj);

        if (red)
        {
            red.SetSDNEventID(id);
            return;
        }
    }

    // =========================================================================
    // WEBHOOK DO DISCORD
    // =========================================================================

    void SendDiscordWebhook(string title, string message)
    {
        if (!m_Config)
        {
            return;
        }

        if (m_Config.DiscordWebhookURL == "")
        {
            return;
        }

        RestApi restApi = GetRestApi();

        if (!restApi)
        {
            restApi = CreateRestApi();
        }

        if (restApi)
        {
            RestContext ctx = restApi.GetRestContext(m_Config.DiscordWebhookURL);

            if (ctx)
            {
                // Limpeza básica para evitar que aspas quebrem o JSON enviado
                message.Replace("\"", "'");
                message.Replace("\n", " ");
                message.Replace("\r", "");
                title.Replace("\"", "'");
                title.Replace("\n", " ");
                title.Replace("\r", "");

                string payload = "{\"username\": \"SDN Container Event\", \"content\": \"**" + title + "**\\n" + message + "\"}";

                // [SDN_FIX] O cabeçalho OBRIGATÓRIO para o Discord não rejeitar o nosso texto!
                ctx.SetHeader("application/json");

                ctx.POST(null, "", payload);
            }
        }
    }

    // =========================================================================
    // CRIAÇÃO DE CONFIGURAÇÃO
    // =========================================================================

    void CreateDefaultConfig()
    {
        m_Config = new SDN_ContainerEventConfig();
        m_Config.EventActive = 1;
        m_Config.EnableSound = true;
        m_Config.ServerTimeOffset = 0;

        m_Config.DiscordWebhookURL = "";

        m_Config.MinPlayers = 1;
        m_Config.SpawnStartDelay = 5;
        m_Config.MaxSimultaneousEvents = 2;
        m_Config.DelayBetweenSpawns = 25;
        m_Config.RespawnTime = 60;
        m_Config.ActivationRadius = 50.0;
        m_Config.TimeToActivate = 30;
        m_Config.TimeAfterActivation = 20;
        m_Config.ZombieCount = 20;
        m_Config.MinLoot = 10;
        m_Config.MaxLoot = 12;
        m_Config.MaxHistoryLocations = 8;

        m_Config.ChanceBlue = 45;
        m_Config.ChanceYellow = 35;
        m_Config.ChanceOrange = 15;
        m_Config.ChanceRed = 5;

        m_Config.LootContainerObject = "CONTAINER_Chest";

        m_Config.TitleSpawn = "NEW EVENT";
        m_Config.TitleActive = "EVENT ACTIVATED";
        m_Config.TitleComplete = "SUCCESS";
        m_Config.TitleFail = "FAILURE";

        m_Config.MsgSpawn = "ATTENTION: A container has been left in the %1 area. The key is with the local patrol!";
        m_Config.MsgActive = "ALERT: Movement detected in the container area!";
        m_Config.MsgComplete = "SUCCESS: A brave survivor looted the container in!";
        m_Config.MsgFail = "FAILURE: No one dared to go to %1. The container was destroyed.";

        m_Config.RestartTimes = new array<string>;
        m_Config.RestartTimes.Insert("00:00");
        m_Config.RestartTimes.Insert("04:00");
        m_Config.RestartTimes.Insert("08:00");
        m_Config.RestartTimes.Insert("12:00");
        m_Config.RestartTimes.Insert("16:00");
        m_Config.RestartTimes.Insert("20:00");

        m_Config.RestartSafetyMinutes = 15;
        m_Config.GhostCleanupTime = 10;

        SDN_EventLocation loc;

        loc = new SDN_EventLocation();
        loc.Name = "Altar Radio";
        loc.Coordinates = "8141 0 9153";
        m_Config.Locations.Insert(loc);

        loc = new SDN_EventLocation();
        loc.Name = "Kabanino Fields";
        loc.Coordinates = "5131 0 8616";
        m_Config.Locations.Insert(loc);

        loc = new SDN_EventLocation();
        loc.Name = "Gorka East";
        loc.Coordinates = "10083 0 8867";
        m_Config.Locations.Insert(loc);

        loc = new SDN_EventLocation();
        loc.Name = "Stary Sobor Road";
        loc.Coordinates = "6697 0 7538";
        m_Config.Locations.Insert(loc);

        loc = new SDN_EventLocation();
        loc.Name = "Mogilevka Road";
        loc.Coordinates = "7653 0 4915";
        m_Config.Locations.Insert(loc);

        loc = new SDN_EventLocation();
        loc.Name = "Green Mtn. Road";
        loc.Coordinates = "3728 0 6013";
        m_Config.Locations.Insert(loc);

        loc = new SDN_EventLocation();
        loc.Name = "Vyshnoye";
        loc.Coordinates = "6411 0 6199";
        m_Config.Locations.Insert(loc);

        loc = new SDN_EventLocation();
        loc.Name = "Kamenca Road";
        loc.Coordinates = "2203 0 2248";
        m_Config.Locations.Insert(loc);

        loc = new SDN_EventLocation();
        loc.Name = "Balota AirStrip";
        loc.Coordinates = "5074 0 2394";
        m_Config.Locations.Insert(loc);

        loc = new SDN_EventLocation();
        loc.Name = "Dolina";
        loc.Coordinates = "11232 0 6551";
        m_Config.Locations.Insert(loc);

        loc = new SDN_EventLocation();
        loc.Name = "Shakhovka Road";
        loc.Coordinates = "9617 0 6583";
        m_Config.Locations.Insert(loc);

        loc = new SDN_EventLocation();
        loc.Name = "Staroy Gas Station";
        loc.Coordinates = "10148 0 5216";
        m_Config.Locations.Insert(loc);

        loc = new SDN_EventLocation();
        loc.Name = "Kamensk Mine Road";
        loc.Coordinates = "7029 0 14482";
        m_Config.Locations.Insert(loc);

        loc = new SDN_EventLocation();
        loc.Name = "Severograd Quarry";
        loc.Coordinates = "7953 0 12693";
        m_Config.Locations.Insert(loc);

        loc = new SDN_EventLocation();
        loc.Name = "Drozhino Village";
        loc.Coordinates = "3297 0 5079";
        m_Config.Locations.Insert(loc);
    }

    void AddDefaultItem(SDN_LootTierConfig cfg, string name, int prob, array<string> atts)
    {
        SDN_EventLootItem item = new SDN_EventLootItem();
        item.ItemName = name;
        item.Probability = prob;
        item.Attachments = atts;
        cfg.Items.Insert(item);
    }

    void CreateDefaultLootTiers()
    {
        int i;
        string path;
        SDN_LootTierConfig tierCfg;

        for (i = 1; i <= 4; i++)
        {
            path = CONFIG_ROOT + "/LootTier" + i + ".json";

            if (!FileExist(path))
            {
                tierCfg = new SDN_LootTierConfig();

                switch(i)
                {
                    case 1:
                    {
                        AddDefaultItem(tierCfg, "SodaCan_Pepsi", 100, {});
                        AddDefaultItem(tierCfg, "TunaCan", 100, {});
                        AddDefaultItem(tierCfg, "BandageDressing", 100, {});
                        AddDefaultItem(tierCfg, "DuctTape", 80, {});
                        AddDefaultItem(tierCfg, "Battery9V", 80, {});
                        AddDefaultItem(tierCfg, "Headtorch_Grey", 70, {"Battery9V"});
                        AddDefaultItem(tierCfg, "HuntingKnife", 60, {});
                        AddDefaultItem(tierCfg, "Machete", 60, {});
                        AddDefaultItem(tierCfg, "MountainBag_Green", 50, {});
                        AddDefaultItem(tierCfg, "Raincoat_Green", 60, {});
                        AddDefaultItem(tierCfg, "CargoPants_Green", 60, {});
                        AddDefaultItem(tierCfg, "HikingBoots_Brown", 60, {});
                        AddDefaultItem(tierCfg, "IJ70", 50, {"Mag_IJ70_8Rnd"});
                        AddDefaultItem(tierCfg, "BK18", 50, {});
                        AddDefaultItem(tierCfg, "AmmoBox_357_20Rnd", 50, {});
                        AddDefaultItem(tierCfg, "AmmoBox_762x39_20Rnd", 50, {});
                        AddDefaultItem(tierCfg, "NailBox", 40, {});
                        AddDefaultItem(tierCfg, "Hammer", 40, {});
                        AddDefaultItem(tierCfg, "Handsaw", 40, {});
                        AddDefaultItem(tierCfg, "Shovel", 30, {});
                        break;
                    }
                    case 2:
                    {
                        AddDefaultItem(tierCfg, "BakedBeansCan", 100, {});
                        AddDefaultItem(tierCfg, "Canteen", 100, {});
                        AddDefaultItem(tierCfg, "Morphine", 80, {});
                        AddDefaultItem(tierCfg, "Epinephrine", 80, {});
                        AddDefaultItem(tierCfg, "SalineBagIV", 70, {});
                        AddDefaultItem(tierCfg, "PoliceVest", 60, {});
                        AddDefaultItem(tierCfg, "PoliceJacket", 60, {});
                        AddDefaultItem(tierCfg, "PolicePants", 60, {});
                        AddDefaultItem(tierCfg, "CombatBoots_Black", 60, {});
                        AddDefaultItem(tierCfg, "Mich2001Helmet", 50, {});
                        AddDefaultItem(tierCfg, "MP5K", 50, {"Mag_MP5_30Rnd", "MP5_PlasticHndgrd", "MP5_RailHndgrd"});
                        AddDefaultItem(tierCfg, "Glock19", 50, {"Mag_Glock_15Rnd"});
                        AddDefaultItem(tierCfg, "Mosin9130", 40, {});
                        AddDefaultItem(tierCfg, "PUScope", 40, {});
                        AddDefaultItem(tierCfg, "AmmoBox_9x19_25Rnd", 60, {});
                        AddDefaultItem(tierCfg, "AmmoBox_762x54_20Rnd", 50, {});
                        AddDefaultItem(tierCfg, "Handcuffs", 40, {});
                        AddDefaultItem(tierCfg, "HandcuffKeys", 40, {});
                        AddDefaultItem(tierCfg, "CombatKnife", 50, {});
                        AddDefaultItem(tierCfg, "AliceBag_Black", 40, {});
                        break;
                    }
                    case 3:
                    {
                        AddDefaultItem(tierCfg, "TacticalBacon", 100, {});
                        AddDefaultItem(tierCfg, "FilteredWaterBottle", 90, {});
                        AddDefaultItem(tierCfg, "PlateCarrierVest", 50, {"PlateCarrierHolster", "PlateCarrierPouches"});
                        AddDefaultItem(tierCfg, "HighCapacityVest_Olive", 60, {});
                        AddDefaultItem(tierCfg, "M65Jacket_Olive", 60, {});
                        AddDefaultItem(tierCfg, "CargoPants_Beige", 60, {});
                        AddDefaultItem(tierCfg, "JungleBoots_Olive", 60, {});
                        AddDefaultItem(tierCfg, "BallisticHelmet_Green", 50, {});
                        AddDefaultItem(tierCfg, "AKM", 40, {"AK_WoodBttstck", "AK_WoodHndgrd", "Mag_AKM_30Rnd"});
                        AddDefaultItem(tierCfg, "M4A1", 30, {"M4_OEBttstck", "M4_PlasticHndgrd", "M4_CarryHandleOptic", "Mag_STANAG_30Rnd|Mag_STANAG_60Rnd"});
                        AddDefaultItem(tierCfg, "AK101", 30, {"AK_PlasticBttstck", "AK_PlasticHndgrd", "Mag_AK101_30Rnd"});
                        AddDefaultItem(tierCfg, "PSO1Optic", 40, {});
                        AddDefaultItem(tierCfg, "KobraOptic", 40, {});
                        AddDefaultItem(tierCfg, "AmmoBox_556x45_20Rnd", 60, {});
                        AddDefaultItem(tierCfg, "AmmoBox_762x39_20Rnd", 60, {});
                        AddDefaultItem(tierCfg, "Grenade_RGD5", 30, {});
                        AddDefaultItem(tierCfg, "LandMineTrap", 30, {});
                        AddDefaultItem(tierCfg, "Rangefinder", 40, {"Battery9V"});
                        AddDefaultItem(tierCfg, "TortillaBag", 40, {});
                        AddDefaultItem(tierCfg, "NVGHeadstrap", 50, {});
                        break;
                    }
                    case 4:
                    {
                        AddDefaultItem(tierCfg, "Bear_SteakMeat", 100, {});
                        AddDefaultItem(tierCfg, "Canteen", 100, {});
                        AddDefaultItem(tierCfg, "PlateCarrierVest", 70, {"PlateCarrierHolster", "PlateCarrierPouches"});
                        AddDefaultItem(tierCfg, "GorkaEJacket_Summer", 60, {});
                        AddDefaultItem(tierCfg, "GorkaPants_Summer", 60, {});
                        AddDefaultItem(tierCfg, "CombatBoots_Grey", 60, {});
                        AddDefaultItem(tierCfg, "Mich2001Helmet", 40, {"NVGHeadstrap", "NVGoggles"});
                        AddDefaultItem(tierCfg, "FAL", 30, {"Fal_OeBttstck", "Mag_FAL_20Rnd"});
                        AddDefaultItem(tierCfg, "SVD", 30, {"Mag_SVD_10Rnd", "PSO1Optic", "AK_Suppressor"});
                        AddDefaultItem(tierCfg, "M70_Tundra", 30, {"HuntingOptic"});
                        AddDefaultItem(tierCfg, "VSS", 30, {"Mag_VSS_10Rnd", "PSO11Optic"});
                        AddDefaultItem(tierCfg, "AmmoBox_308Win_20Rnd", 60, {});
                        AddDefaultItem(tierCfg, "AmmoBox_762x54_20Rnd", 60, {});
                        AddDefaultItem(tierCfg, "Grenade_M67", 40, {});
                        AddDefaultItem(tierCfg, "Plastic_Explosive", 20, {});
                        AddDefaultItem(tierCfg, "RemoteDetonator", 20, {});
                        AddDefaultItem(tierCfg, "NVGoggles", 40, {"Battery9V"});
                        AddDefaultItem(tierCfg, "CoyoteBag_Brown", 40, {});
                        AddDefaultItem(tierCfg, "GhillieSuit_Woodland", 20, {});
                        AddDefaultItem(tierCfg, "LargeTent", 10, {});
                        break;
                    }
                }

                JsonFileLoader<SDN_LootTierConfig>.JsonSaveFile(path, tierCfg);
            }
        }
    }

    // =========================================================================
    // LÓGICA DE SPAWN (Zumbis, Loot, Container)
    // =========================================================================

    void SpawnZombies(ActiveEventInstance evt, string keyName, int eventID)
    {
        if (!m_Config)
        {
            return;
        }

        int i;
        vector spawnPos;
        float angle, dist;
        EntityAI zmb;
        int keyHolderIndex = Math.RandomInt(0, 4);
        string bossBerets[] = {"MilitaryBeret_UN", "MilitaryBeret_Red", "MilitaryBeret_ChdkZ", "MilitaryBeret_CDF"};

        for (i = 0; i < m_Config.ZombieCount; i++)
        {
            spawnPos = evt.Position;
            angle = Math.RandomFloat(0, Math.PI * 2);
            dist = Math.RandomFloat(5, 25);

            if (i >= 4)
            {
                dist = Math.RandomFloat(15, 35);
            }

            spawnPos[0] = spawnPos[0] + Math.Cos(angle) * dist;
            spawnPos[2] = spawnPos[2] + Math.Sin(angle) * dist;
            spawnPos[1] = GetGame().SurfaceY(spawnPos[0], spawnPos[2]);

            zmb = EntityAI.Cast(GetGame().CreateObject("ZmbM_PatrolNormal_PautRev", spawnPos, false, true));

            if (zmb)
            {
                // Ensure spawned zombies eventually despawn after dying rather than lasting forever
                zmb.SetLifetime(3600); // 1 hour maximum lifetime

                evt.Zombies.Insert(zmb);

                if (i < 4)
                {
                    zmb.GetInventory().CreateAttachment(bossBerets[i]);

                    if (i == keyHolderIndex)
                    {
                        EntityAI keyObj = zmb.GetInventory().CreateInInventory(keyName);

                        if (keyObj)
                        {
                            SetIDSafe(keyObj, eventID);
                        }
                    }
                }
            }
        }
    }

    void FillLootCrate(EntityAI crate, int tier)
    {
        int lootCount = 0;
        int maxAttempts = m_Config.MaxLoot * 10;
        int attempts = 0;

        SDN_EventLootItem lootConfig;
        EntityAI item, attachment;
        Magazine mag;
        ItemBase itemBase;
        TStringArray options;
        SDN_LootTierConfig tierLoot = new SDN_LootTierConfig();

        string path = CONFIG_ROOT + "/LootTier" + tier + ".json";

        if (FileExist(path))
        {
            JsonFileLoader<SDN_LootTierConfig>.JsonLoadFile(path, tierLoot);
        }
        else
        {
            return;
        }

        if (!crate || !tierLoot.Items)
        {
            return;
        }

        while (lootCount < m_Config.MinLoot && attempts < maxAttempts)
        {
            attempts++;

            if (lootCount >= m_Config.MaxLoot)
            {
                break;
            }

            lootConfig = tierLoot.Items.GetRandomElement();

            if (Math.RandomFloat01() <= (lootConfig.Probability / 100.0))
            {
                item = crate.GetInventory().CreateEntityInCargo(lootConfig.ItemName);

                if (!item)
                {
                    item = crate.GetInventory().CreateInInventory(lootConfig.ItemName);
                }

                if (item)
                {
                    if (lootConfig.Attachments)
                    {
                        foreach (string rawAttName : lootConfig.Attachments)
                        {
                            string selectedAtt = rawAttName;

                            if (rawAttName.Contains("|"))
                            {
                                options = new TStringArray;
                                rawAttName.Split("|", options);
                                selectedAtt = options.GetRandomElement();
                            }

                            attachment = item.GetInventory().CreateAttachment(selectedAtt);

                            if (!attachment)
                            {
                                attachment = item.GetInventory().CreateInInventory(selectedAtt);
                            }

                            if (attachment)
                            {
                                mag = Magazine.Cast(attachment);

                                if (mag)
                                {
                                    mag.ServerSetAmmoCount(mag.GetAmmoMax());
                                }
                            }
                        }
                    }

                    itemBase = ItemBase.Cast(item);

                    if (itemBase && itemBase.CanBeSplit())
                    {
                        itemBase.SetQuantity(Math.RandomFloat(itemBase.GetQuantityMax() * 0.5, itemBase.GetQuantityMax()));
                    }

                    lootCount++;
                }
            }
        }
    }

    void CleanGroundItems(vector pos, float radius)
    {
        array<Object> objects = new array<Object>;
        array<CargoBase> proxy = new array<CargoBase>;
        GetGame().GetObjectsAtPosition(pos, radius, objects, proxy);

        int deletedCount = 0;

        foreach (Object obj : objects)
        {
            EntityAI ent = EntityAI.Cast(obj);

            if (!ent)
            {
                continue;
            }

            if (ent.IsInherited(Man))
            {
                continue;
            }

            if (ent.IsInherited(CarScript))
            {
                continue;
            }

            if (ent.IsInherited(TentBase))
            {
                continue;
            }

            if (ent.IsInherited(BaseBuildingBase))
            {
                continue;
            }

            // Protect player's backpacks, clothing, and other important items from being deleted.
            // Only delete specific event-related items (like smoke grenades or dead bodies).
            string type = ent.GetType();

            if (type.Contains("M18SmokeGrenade") || ent.IsInherited(DayZInfected))
            {
                GetGame().ObjectDelete(ent);
                deletedCount++;
            }
        }
    }

    void HandleCrashGhosts()
    {
        if (m_GhostLocations.Count() > 0)
        {
            Log("Checking " + m_GhostLocations.Count() + " completed events pre-restart...");
            array<Object> objects = new array<Object>;
            array<CargoBase> cargos = new array<CargoBase>;
            string lootName = m_Config.LootContainerObject;

            if (lootName == "")
            {
                lootName = "StorageBox_Wooden";
            }

            int cleaned = 0;

            foreach (vector pos : m_GhostLocations)
            {
                CleanGroundItems(pos, 10.0);

                objects.Clear();
                cargos.Clear();

                GetGame().GetObjectsAtPosition(pos, 10.0, objects, cargos);

                foreach (Object obj : objects)
                {
                    EntityAI ent = EntityAI.Cast(obj);

                    if (ent)
                    {
                        string type = ent.GetType();

                        if (type.Contains("Land_ContainerLocked") || type.Contains("SDN_Test_ContainerBase") || type == lootName)
                        {
                            float cleanupTime = m_Config.GhostCleanupTime * 60;

                            if (cleanupTime <= 0)
                            {
                                cleanupTime = 300;
                            }

                            ent.SetLifetime(cleanupTime);

                            if (type == lootName)
                            {
                                ItemBase chestItem = ItemBase.Cast(ent);

                                if (chestItem)
                                {
                                    chestItem.SetTakeable(false);
                                }
                            }

                            cleaned++;
                        }
                    }
                }
            }

            if (cleaned > 0)
            {
                Log("Adjusted and Locked " + cleaned + " ghost objects.");
            }

            m_GhostLocations.Clear();
            SaveState();
        }
    }

    bool IsRestartComing()
    {
        if (!m_Config.RestartTimes || m_Config.RestartTimes.Count() == 0)
        {
            return false;
        }

        int hour, minute, second;
        GetHourMinuteSecond(hour, minute, second);

        if (m_Config.ServerTimeOffset != 0)
        {
            hour = hour + m_Config.ServerTimeOffset;

            if (hour >= 24)
            {
                hour = hour - 24;
            }
            else if (hour < 0)
            {
                hour = hour + 24;
            }
        }

        int currentTotalMinutes = (hour * 60) + minute;
        int safetyMargin = m_Config.RestartSafetyMinutes;

        foreach (string timeStr : m_Config.RestartTimes)
        {
            TStringArray parts = new TStringArray;
            timeStr.Split(":", parts);

            if (parts.Count() == 2)
            {
                int rHour = parts.Get(0).ToInt();
                int rMinute = parts.Get(1).ToInt();
                int restartTotalMinutes = (rHour * 60) + rMinute;
                int diff = restartTotalMinutes - currentTotalMinutes;

                if (diff > 0 && diff <= safetyMargin)
                {
                    Log("Spawn blocked! Restart scheduled for " + timeStr + " (" + diff + " min remaining).");
                    return true;
                }
            }
        }

        return false;
    }

    void UpdateEventSmoke(ActiveEventInstance evt, float timeslice)
    {
        evt.SmokeTimer += timeslice;
        string targetColor = "M18SmokeGrenade_Green";

        if (evt.IsCompleted)
        {
            targetColor = "M18SmokeGrenade_Red";
        }
        else if (evt.IsActive)
        {
            targetColor = "M18SmokeGrenade_Yellow";
        }

        bool timeToRefresh = evt.SmokeTimer >= 40.0;
        bool colorChanged = evt.CurrentSmokeColor != targetColor;
        bool smokeMissing = !evt.VisualEffect;

        if (timeToRefresh || colorChanged || smokeMissing)
        {
            if (evt.VisualEffect)
            {
                GetGame().ObjectDelete(evt.VisualEffect);
            }

            vector smokePos = evt.Position;
            smokePos[1] = smokePos[1] + 2.5;

            EntityAI smokeObj = EntityAI.Cast(GetGame().CreateObject(targetColor, smokePos, false, true));

            if (smokeObj)
            {
                ItemBase smokeItem = ItemBase.Cast(smokeObj);

                if (smokeItem)
                {
                    smokeItem.SetTakeable(false);
                    smokeItem.SetLifetime(3600);

                    if (smokeItem.GetCompEM())
                    {
                        smokeItem.GetCompEM().SwitchOn();
                    }

                    evt.VisualEffect = smokeItem;
                    evt.CurrentSmokeColor = targetColor;
                    evt.SmokeTimer = 0;
                }
            }
        }
    }

    // --- CONFIGURAÇÃO E ESTADO ---


    void InitSceneTemplate()
    {
        m_SceneTemplate = new array<ref SDN_SceneObjectTemplate>;
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/556_20roundbox.p3d", "-7.6450 -2.3608 1.4844", "2.0908 1.5708 -1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/9mm_looserounds.p3d", "-0.2886 -1.0723 2.4385", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/9mm_25rnd_box.p3d", "-0.1782 -1.0216 1.6416", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/556_looserounds.p3d", "0.1162 -1.0768 0.6230", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/00buck_10roundbox.p3d", "-0.8823 0.1916 2.5762", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/308win_looserounds.p3d", "0.4888 -1.0644 1.8955", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/762_20roundbox.p3d", "0.0625 -1.0641 1.6025", "-2.8972 1.5708 -1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/40mm_grenade.p3d", "-0.0215 -1.0314 0.6670", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/556_20roundbox.p3d", "-0.0410 -1.0829 1.4170", "-2.7576 1.5708 -1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/308win_looserounds.p3d", "-0.6157 0.1428 2.5332", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/556_looserounds.p3d", "0.1152 -1.0768 0.6797", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/9x39_looserounds.p3d", "-0.8311 -1.0536 1.7031", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/308win_looserounds.p3d", "-0.1079 -1.0644 2.2285", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/9x39_looserounds.p3d", "-0.2632 -1.0769 1.5674", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/9mm_25rnd_box.p3d", "-0.5088 -1.0262 1.0615", "0.3142 0.6807 0.0873"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/556_20roundbox.p3d", "0.0542 -1.0947 1.7119", "-2.9845 1.5708 -1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/762_looserounds.p3d", "-0.7441 0.1415 2.4336", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/12ga_00buck.p3d", "-0.7827 0.1453 2.6211", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/762_20roundbox.p3d", "-0.4814 0.1798 2.5645", "-2.8972 1.5708 -1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/12ga_00buck.p3d", "-0.5659 0.1486 2.6279", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/gear/tools/shippingcontainerkeys.p3d", "23.0205 -2.4263 11.4463", "-2.5339 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures_bliss/underground/storage/proxy/ammoboxes_single.p3d", "0.2471 -1.0944 -2.0195", "1.5650 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures_bliss/underground/storage/proxy/ammoboxes_single.p3d", "-0.5879 -0.4016 -1.9951", "1.5542 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures_bliss/underground/storage/proxy/ammoboxes_single.p3d", "-0.6118 0.1291 -0.2764", "-1.5529 0.0000 0.6981"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures/wrecks/vehicles/wreck_decal_small2.p3d", "1.3638 3.4179 -8.0234", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures/wrecks/vehicles/wreck_decal_small1_nolc.p3d", "3.5381 -2.4634 -10.3252", "0.3142 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures/wrecks/vehicles/wreck_decal_small1_nolc.p3d", "3.8403 -2.4730 -4.0986", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures/wrecks/vehicles/wreck_uaz.p3d", "3.7993 -2.5396 -3.5098", "0.0524 0.1920 -3.1055"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/vehicles/wheeled/truck_01/proxy/truck_01_wheel_ruined.p3d", "0.2261 -2.3371 5.0957", "0.0524 0.0000 1.6406"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/vehicles/wheeled/truck_01/proxy/truck_01_wheeldouble_ruined.p3d", "-0.2925 -2.1839 4.5801", "2.3717 -0.0349 0.9948"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/vehicles/wheeled/offroad_02/proxy/offroad_02_wheel_destroyed.p3d", "2.4443 -2.0943 3.7900", "-0.3142 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures_takistan/wrecks/wheeled/wreck_truck02_green.p3d", "0.4243 -2.4564 -5.1367", "-1.7961 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures_bliss/underground/storage/proxy/ammoboxes_single.p3d", "-0.4463 0.4448 -2.1064", "1.5542 -1.0472 0.0175"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StorageBox_Wooden", "-0.5786 -1.0843 -0.0459", "-90.9490 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SDN_P_fire_tirepile", "1.2583 -0.6232 -8.0576", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SDN_P_smoke_bonfire", "2.5015 -1.4990 5.2900", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SDN_P_fire_medium_oven_01", "3.4653 -1.4196 -5.0439", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SDN_P_smoke_small_barrel_01", "3.4453 -1.5038 -5.0186", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures/furniture/cases/paperbox/paperbox_01_small_closed.p3d", "-0.6685 -1.0966 2.5312", "0.0054 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SDN_P_fire_small_oven_01", "3.6680 -1.6163 -8.2041", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SDN_P_smoke_medium_house_01", "3.6724 -1.6200 -8.2061", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures/wrecks/vehicles/wreck_decal_small2_nolc.p3d", "1.0137 -2.4628 4.4160", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/svd/svd_old.p3d", "0.3330 -0.7246 1.4209", "0.0000 -0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/svd/svd_old.p3d", "0.6025 -0.7246 1.5176", "0.0000 -0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/m14/m14.p3d", "0.8784 -0.6484 1.7168", "0.0000 -0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/svd/svd.p3d", "0.3242 -0.7099 1.5137", "0.0000 -0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/m14/m14.p3d", "0.8667 -0.6484 1.6133", "0.0000 -0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/m14/m14.p3d", "0.8823 -0.6484 1.5127", "0.0000 -0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/svd/svd.p3d", "0.5938 -0.7099 1.6104", "0.0000 -0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/svd/svd.p3d", "0.3311 -0.7099 1.6182", "0.0000 -0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/svd/svd.p3d", "0.6006 -0.7099 1.7148", "0.0000 -0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures/military/misc/misc_gunrack.p3d", "0.6514 -1.1127 1.8174", "0.0000 -0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/sv98/sv98.p3d", "0.6133 -0.7079 2.0068", "-0.0407 0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/sv98/sv98.p3d", "0.8911 -0.7079 2.0117", "-0.0407 -0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/sv98/sv98.p3d", "0.6235 -0.7079 2.1094", "-0.0407 0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/sv98/sv98.p3d", "0.9014 -0.7079 2.1143", "-0.0407 0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures_takistan/wrecks/wheeled/wreck_pickup01_armed_tk.p3d", "2.0645 -1.4321 -9.8037", "-2.7287 -0.0307 -1.8569"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/sv98/sv98.p3d", "0.6323 -0.7079 2.2168", "-0.0407 0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures_takistan/wrecks/wheeled/wreck_lr_armed_tk.p3d", "2.3350 -2.5510 5.4775", "-0.4712 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/762x39_looserounds.p3d", "0.2144 -1.5194 0.7852", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/556_looserounds.p3d", "0.1343 -1.0932 1.0137", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/545x39_20roundbox.p3d", "-0.4419 0.1510 2.5918", "-3.0172 1.5705 3.1416"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/545x39_20roundbox.p3d", "-0.6450 0.1457 2.3926", "-3.0172 1.5708 3.1416"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/545x39_20roundbox.p3d", "-0.7417 -1.0363 1.7998", "0.1244 1.5708 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures_bliss/underground/storage/proxy/ammoboxes_single.p3d", "-0.5825 -0.7561 -1.9932", "1.5542 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures_bliss/underground/storage/proxy/ammoboxes_single.p3d", "-0.6064 -1.1107 -1.9648", "1.5923 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/firearms/sv98/sv98.p3d", "0.9102 -0.7079 2.2227", "-0.0407 0.0000 1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures_takistan/wrecks/wheeled/wreck_trailer_single_container.p3d", "-0.0352 -2.4557 -0.8828", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures_bliss/underground/storage/proxy/ammoboxes_single.p3d", "-0.5903 -0.0471 -2.1064", "1.5542 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures/furniture/cases/paperbox/paperbox_01_small_closed.p3d", "-0.6685 -0.6848 2.5273", "0.0054 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/762x39_20roundbox.p3d", "3.3145 -2.3037 5.7617", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/762x39_20roundbox.p3d", "-0.6567 -0.2312 0.4727", "-0.9810 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/762x39_20roundbox.p3d", "-0.4810 -0.2312 0.6221", "-0.9810 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/762x39_20roundbox.p3d", "-0.1470 -1.0396 1.5684", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/762_20roundbox.p3d", "-0.4233 -0.2779 0.7617", "2.1606 1.5708 -1.5708"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/00buck_10roundbox.p3d", "-0.7412 -0.2327 0.4678", "-0.9810 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/762_looserounds.p3d", "-0.5435 -0.2821 0.6621", "-0.9810 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/structures/furniture/cases/paperbox/paperbox_01_small_closed.p3d", "-0.6738 -0.2730 2.5264", "0.0054 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("dz/weapons/ammunition/00buck_10roundbox.p3d", "-0.8433 0.1923 2.5020", "1.2401 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("FireplaceLight", "0.5474 0.0479 -8.5342", "0.0000 0.0000 -0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("FireplaceLight", "3.3716 -1.3767 -5.0322", "0.0000 0.0000 -0.0000"));

    }

    void LoadConfig()
    {
        if (FileExist(CONFIG_PATH))
        {
            JsonFileLoader<SDN_ContainerEventConfig>.JsonLoadFile(CONFIG_PATH, m_Config);
            Log("Configuration loaded.");
        }
        else
        {
            CreateDefaultConfig();
            JsonFileLoader<SDN_ContainerEventConfig>.JsonSaveFile(CONFIG_PATH, m_Config);
            Log("New default configuration created.");
        }

        CreateDefaultLootTiers();

        if (m_Config)
        {
            if (m_CurrentTargetTime == 0)
            {
                m_CurrentTargetTime = m_Config.SpawnStartDelay * 60;
            }
        }
    }

    void SaveState()
    {
        if (!m_IsInitialized)
        {
            return;
        }

        SDN_ContainerEventGlobalState state = new SDN_ContainerEventGlobalState();
        state.SpawnTimer = m_SpawnTimer;
        state.CurrentTargetTime = m_CurrentTargetTime;
        state.EventsSpawnedInWave = m_EventsSpawnedInWave;
        state.LastRoundLocations = m_HistoryLocations;
        state.CurrentWaveLocations = m_CurrentWaveLocations;

        state.CompletedGhostLocations = m_GhostLocations;

        foreach (ActiveEventInstance evt : m_ActiveEvents)
        {
            if (evt.IsCompleted)
            {
                continue;
            }

            SDN_SavedEventData data = new SDN_SavedEventData();
            data.Position = evt.Position;
            data.LocationName = evt.LocationName;
            data.ContainerType = evt.ContainerType;
            data.KeyType = evt.KeyType;
            data.Tier = evt.Tier;
            data.LifeTime = evt.LifeTime;
            data.InitialLootCount = evt.InitialLootCount;
            state.ActiveEvents.Insert(data);
        }

        JsonFileLoader<SDN_ContainerEventGlobalState>.JsonSaveFile(STATE_PATH, state);
    }

    void LoadState()
    {
        if (!FileExist(STATE_PATH))
        {
            return;
        }

        SDN_ContainerEventGlobalState state = new SDN_ContainerEventGlobalState();
        JsonFileLoader<SDN_ContainerEventGlobalState>.JsonLoadFile(STATE_PATH, state);

        m_SpawnTimer = state.SpawnTimer;
        m_CurrentTargetTime = state.CurrentTargetTime;
        m_EventsSpawnedInWave = state.EventsSpawnedInWave;

        if (state.LastRoundLocations)
        {
            m_HistoryLocations = state.LastRoundLocations;
        }

        if (state.CurrentWaveLocations)
        {
            m_CurrentWaveLocations = state.CurrentWaveLocations;
        }

        if (state.CompletedGhostLocations)
        {
            m_GhostLocations = state.CompletedGhostLocations;
        }

        if (state.ActiveEvents)
        {
            Log("Trying to reconnect " + state.ActiveEvents.Count() + " persistent events...");

            foreach (SDN_SavedEventData data : state.ActiveEvents)
            {
                SpawnEventFromSave(data);
            }
        }
    }

    void SpawnEventFromSave(SDN_SavedEventData data)
    {
        SDN_EventLocation loc = new SDN_EventLocation();
        loc.Name = data.LocationName;
        loc.Coordinates = data.Position;
        SpawnEventInternal(loc, data.ContainerType, data.KeyType, data.Tier, 0, data.InitialLootCount, true);
    }

    // --- MÉTODOS DE LOOP PRINCIPAL ---

    void OnUpdate(float timeslice)
    {
        if (!m_Config || m_Config.EventActive == 0)
        {
            return;
        }

        m_UpdateTimer += timeslice;

        if (!m_IsInitialized)
        {
            m_InitDelayTimer += timeslice;

            if (m_InitDelayTimer >= 25.0)
            {
                RunDelayedInit();
            }
            return;
        }

        if (m_UpdateTimer >= 1.0)
        {
            CheckActiveEvents(1.0);
            m_SpawnTimer += 1.0;
            m_SaveTimer += 1.0;

            if (m_SaveTimer >= 60.0)
            {
                SaveState();
                m_SaveTimer = 0;
            }

            if (m_SpawnTimer >= m_CurrentTargetTime)
            {
                if (IsRestartComing())
                {
                    m_SpawnTimer = m_CurrentTargetTime - 60;
                    m_UpdateTimer = 0;
                    return;
                }

                array<Man> players = new array<Man>;
                GetGame().GetPlayers(players);

                if (players.Count() >= m_Config.MinPlayers)
                {
                    if (TrySpawnEvent())
                    {
                        m_EventsSpawnedInWave++;

                        if (m_EventsSpawnedInWave >= m_Config.MaxSimultaneousEvents)
                        {
                            m_HistoryLocations.InsertAll(m_CurrentWaveLocations);

                            int historyLimit = 20;

                            if (m_Config.MaxHistoryLocations > 0)
                            {
                                historyLimit = m_Config.MaxHistoryLocations;
                            }

                            while (m_HistoryLocations.Count() > historyLimit)
                            {
                                m_HistoryLocations.Remove(0);
                            }

                            m_CurrentWaveLocations.Clear();
                            m_CurrentTargetTime = m_Config.RespawnTime * 60;
                            m_EventsSpawnedInWave = 0;
                            Log("Wave finished. History updated (Max " + historyLimit + "). Next in " + m_Config.RespawnTime + " min.");
                            SaveState();
                        }
                        else
                        {
                            m_CurrentTargetTime = m_Config.DelayBetweenSpawns * 60;
                            Log("Event spawned. Next in " + m_Config.DelayBetweenSpawns + " min.");
                        }

                        m_SpawnTimer = 0;
                        SaveState();
                    }
                    else
                    {
                        m_SpawnTimer = m_CurrentTargetTime - 60;
                    }
                }
                else
                {
                    m_SpawnTimer = m_CurrentTargetTime - 60;
                }
            }

            m_UpdateTimer = 0;
        }
    }

    bool TrySpawnEvent()
    {
        array<Man> players = new array<Man>;
        SDN_EventLocation randomLoc;
        int maxRetries = 10;
        int attempt = 0;
        bool foundUnique = false;

        if (!m_Config)
        {
            return false;
        }

        if (m_ActiveEvents.Count() >= m_Config.MaxSimultaneousEvents)
        {
            return false;
        }

        if (!m_Config.Locations || m_Config.Locations.Count() == 0)
        {
            return false;
        }

        while (attempt < maxRetries)
        {
            attempt++;
            randomLoc = m_Config.Locations.Get(Math.RandomInt(0, m_Config.Locations.Count()));

            if (randomLoc.Name == m_LastSpawnedLocName)
            {
                continue;
            }

            if (m_CurrentWaveLocations.Find(randomLoc.Name) != -1)
            {
                continue;
            }

            if (m_HistoryLocations.Find(randomLoc.Name) != -1)
            {
                continue;
            }

            bool tooClose = false;

            foreach (ActiveEventInstance existing : m_ActiveEvents)
            {
                if (vector.Distance(existing.Position, randomLoc.Coordinates) < 200)
                {
                    tooClose = true;
                    break;
                }
            }

            if (tooClose)
            {
                continue;
            }

            foundUnique = true;
            break;
        }

        if (!foundUnique)
        {
            return false;
        }

        SpawnEvent(randomLoc);
        return true;
    }

    void SpawnEvent(SDN_EventLocation loc)
    {
        m_LastSpawnedLocName = loc.Name;
        m_CurrentWaveLocations.Insert(loc.Name);
        SaveState();

        string cType, kType;
        int tier;
        int roll = Math.RandomInt(0, 100);
        int currentChance = 0;

        currentChance += m_Config.ChanceBlue;

        if (roll < currentChance)
        {
            tier = 1;
            cType = "Land_ContainerLocked_Blue_DE";
            kType = "ShippingContainerKeys_Blue";
        }
        else
        {
            currentChance += m_Config.ChanceYellow;

            if (roll < currentChance)
            {
                tier = 2;
                cType = "Land_ContainerLocked_Yellow_DE";
                kType = "ShippingContainerKeys_Yellow";
            }
            else
            {
                currentChance += m_Config.ChanceOrange;

                if (roll < currentChance)
                {
                    tier = 3;
                    cType = "Land_ContainerLocked_Orange_DE";
                    kType = "ShippingContainerKeys_Orange";
                }
                else
                {
                    tier = 4;
                    cType = "Land_ContainerLocked_Red_DE";
                    kType = "ShippingContainerKeys_Red";
                }
            }
        }

        SpawnEventInternal(loc, cType, kType, tier, 0, 0, false);

        string msg = m_Config.MsgSpawn;

        if (msg.Contains("%1"))
        {
            msg = string.Format(msg, loc.Name);
        }
        else
        {
            msg = msg + " [" + loc.Name + "]";
        }

        SDN_SendNotificationToAll(m_Config.TitleSpawn, msg, ICON_PATH, 10.0);
    }

    void SpawnEventInternal(SDN_EventLocation loc, string cType, string kType, int tier, float lifeTime, int initialLoot, bool isRestoration)
    {
        ActiveEventInstance newEvent = new ActiveEventInstance();
        vector spawnPos = loc.Coordinates;
        spawnPos[1] = GetGame().SurfaceY(spawnPos[0], spawnPos[2]);

        newEvent.LocationName = loc.Name;
        newEvent.Position = spawnPos;
        newEvent.ContainerType = cType;
        newEvent.KeyType = kType;
        newEvent.Tier = tier;
        newEvent.LifeTime = lifeTime;

        int eventID = Math.RandomInt(100000, 999999);

        EntityAI containerObj = null;

        if (isRestoration)
        {
            array<Object> nearbyObjs = new array<Object>;
            array<CargoBase> proxies = new array<CargoBase>;
            GetGame().GetObjectsAtPosition(spawnPos, 3.0, nearbyObjs, proxies);

            foreach (Object found : nearbyObjs)
            {
                if (found.GetType() == cType)
                {
                    containerObj = EntityAI.Cast(found);
                    Log(" Container found (Safe Start)! Reconnecting event at " + loc.Name);
                    
                    // Retrieve existing ID
                    Land_ContainerLocked_Blue_DE blue = Land_ContainerLocked_Blue_DE.Cast(containerObj);
                    if (blue) eventID = blue.GetSDNEventID();
                    
                    Land_ContainerLocked_Yellow_DE yellow = Land_ContainerLocked_Yellow_DE.Cast(containerObj);
                    if (yellow) eventID = yellow.GetSDNEventID();
                    
                    Land_ContainerLocked_Orange_DE orange = Land_ContainerLocked_Orange_DE.Cast(containerObj);
                    if (orange) eventID = orange.GetSDNEventID();
                    
                    Land_ContainerLocked_Red_DE red = Land_ContainerLocked_Red_DE.Cast(containerObj);
                    if (red) eventID = red.GetSDNEventID();
                    
                    break;
                }
            }
        }

        if (!containerObj)
        {
            CleanGroundItems(spawnPos, 15.0);

            array<Object> objects_nearby = new array<Object>;
            array<CargoBase> proxy_cargos = new array<CargoBase>;

            string lootObjName = m_Config.LootContainerObject;

            if (lootObjName == "")
            {
                lootObjName = "StorageBox_Wooden";
            }

            GetGame().GetObjectsAtPosition(spawnPos, 15.0, objects_nearby, proxy_cargos);

            foreach (Object obj : objects_nearby)
            {
                EntityAI ent = EntityAI.Cast(obj);

                if (ent)
                {
                    string entType = ent.GetType();

                    if (entType.Contains("Land_ContainerLocked") || entType.Contains("SDN_Test_ContainerBase") || entType == lootObjName || entType == "Barrel_Red")
                    {
                        GetGame().ObjectDelete(ent);
                    }
                }
            }

            containerObj = EntityAI.Cast(GetGame().CreateObject(cType, spawnPos, false, true));

            if (!containerObj)
            {
                return;
            }

            containerObj.SetPosition(spawnPos);
            containerObj.SetOrientation("0 0 0");
            containerObj.PlaceOnSurface();

            SetIDSafe(containerObj, eventID);
        }

        newEvent.Container = containerObj;
        UpdateEventSmoke(newEvent, 100.0);

        EntityAI crate = null;
        vector cratePos = spawnPos;
        cratePos[1] = cratePos[1] + 0.10;

        string chestName = m_Config.LootContainerObject;

        if (chestName == "")
        {
            chestName = "StorageBox_Wooden";
        }

        if (isRestoration)
        {
             array<Object> nearbyChests = new array<Object>;
             array<CargoBase> cProxies = new array<CargoBase>;
             GetGame().GetObjectsAtPosition(spawnPos, 3.0, nearbyChests, cProxies);

             foreach (Object cObj : nearbyChests)
             {
                 if (cObj.GetType() == chestName)
                 {
                     crate = EntityAI.Cast(cObj);
                     break;
                 }
             }
        }

        if (!crate)
        {
            crate = EntityAI.Cast(GetGame().CreateObject(chestName, cratePos, false, true));

            if (crate)
            {
                crate.SetPosition(cratePos);
                vector crateOri = containerObj.GetOrientation();
                crateOri[0] = crateOri[0] + 90;
                crate.SetOrientation(crateOri);
                FillLootCrate(crate, tier);
            }
        }

        if (crate)
        {
            ItemBase itemCrate = ItemBase.Cast(crate);

            if (itemCrate)
            {
                itemCrate.SetTakeable(false);
                itemCrate.SetAllowDamage(false);
                itemCrate.SetLifetime(7200);
            }

            newEvent.LootCrate = itemCrate;

            if (initialLoot == 0)
            {
                newEvent.InitialLootCount = crate.GetInventory().CountInventory();
            }
            else
            {
                newEvent.InitialLootCount = initialLoot;
            }
        }

        SpawnZombies(newEvent, kType, eventID);
        if (!isRestoration) SpawnSceneObjects(newEvent, spawnPos, containerObj.GetOrientation());

        m_ActiveEvents.Insert(newEvent);

        if (m_Config && m_Config.MarkerSettings)
        {
            SDN_MarkerManager.AddEventMarker(loc.Name, spawnPos, m_Config.MarkerSettings);
        }

        Log("Event Active and Linked at " + loc.Name + " [ID: " + eventID + "]");
    }


    void SpawnSceneObjects(ActiveEventInstance evt, vector basePos, vector baseOri)
    {
        if (!m_SceneTemplate) return;

        // DayZ uses Yaw Pitch Roll. The math for orienting is complex natively,
        // so we can build a matrix if needed. But let's keep it simple:
        // if baseOri is [0,0,0], we just add the offsets. If random rotation is added later,
        // we'd need quaternion math. For now, assuming base orientation is 0,0,0 or handling simple Y-axis yaw.

        // Fix backwards DayZ coordinate math: rotate clockwise
        float yaw = -baseOri[0] * Math.DEG2RAD;
        float cosY = Math.Cos(yaw);
        float sinY = Math.Sin(yaw);

        foreach (SDN_SceneObjectTemplate tmpl : m_SceneTemplate)
        {
            vector localPos = tmpl.OffsetPos;
            vector worldPos = "0 0 0";

            // Apply 2D Rotation (Yaw) correctly for DayZ Engine
            worldPos[0] = basePos[0] + (localPos[0] * cosY - localPos[2] * sinY);
            worldPos[1] = basePos[1] + localPos[1];
            worldPos[2] = basePos[2] + (localPos[0] * sinY + localPos[2] * cosY);

            vector worldOri = tmpl.OffsetOri;
            worldOri[0] = worldOri[0] + baseOri[0];

            Object obj = null;
            if (tmpl.ClassName.Contains(".p3d"))
            {
                obj = GetGame().CreateObjectEx(tmpl.ClassName, worldPos, ECE_SETUP | ECE_CREATEPHYSICS | ECE_NOLIFETIME);
            }
            else
            {
                obj = GetGame().CreateObject(tmpl.ClassName, worldPos, false, true);
            }

            if (obj)
            {
                obj.SetPosition(worldPos);
                obj.SetOrientation(worldOri);

                // Align to terrain properly if needed (some objects like cars shouldn't snap strictly,
                // but let's let DayZ physics handle it if it drops, or force it)
                // obj.PlaceOnSurface();

                evt.SceneObjects.Insert(obj);
            }
        }
    }

    void CheckActiveEvents(float delta)
    {
        int i;
        ActiveEventInstance evt;
        bool isContainerOpen = false;
        string msg;
        int j;
        string anims[] = {"DoorsTwin1", "DoorsTwin2", "Doors1", "Doors2", "Door_1_1", "Door_1_2", "Door_2_1"};

        for (i = m_ActiveEvents.Count() - 1; i >= 0; i--)
        {
            evt = m_ActiveEvents.Get(i);
            evt.LifeTime += delta;
            evt.DebugTimer += delta;

            UpdateEventSmoke(evt, delta);

            if (evt.LootCrate)
            {
                evt.LootCrate.SetTakeable(false);
            }

            if (!evt.IsCompleted && evt.Container)
            {
                isContainerOpen = false;

                for (j = 0; j < 7; j++)
                {
                    if (evt.Container.GetAnimationPhase(anims[j]) > 0.5)
                    {
                        isContainerOpen = true;
                        break;
                    }
                }

                if (!isContainerOpen)
                {
                    ItemBase ib = ItemBase.Cast(evt.Container);

                    if (ib && ib.IsOpen())
                    {
                        isContainerOpen = true;
                    }
                }

                if (!isContainerOpen && evt.LootCrate && evt.LifeTime > 5.0)
                {
                     if (evt.LootCrate.GetInventory().CountInventory() < evt.InitialLootCount)
                     {
                         isContainerOpen = true;
                     }
                }

                if (isContainerOpen)
                {
                    evt.IsCompleted = true;

                    msg = m_Config.MsgComplete;

                    if (msg.Contains("%1"))
                    {
                        msg = string.Format(msg, evt.LocationName);
                    }
                    else
                    {
                        msg = msg + " [" + evt.LocationName + "]";
                    }

                    SDN_SendNotificationToAll(m_Config.TitleComplete, msg, ICON_PATH, 10.0);

                    Log("Event Completed at " + evt.LocationName);

                    CleanGroundItems(evt.Position, 10.0);

                    if (m_Config && m_Config.MarkerSettings)
                    {
                        SDN_MarkerManager.RemoveEventMarker(evt.LocationName, m_Config.MarkerSettings);
                    }

                    m_GhostLocations.Insert(evt.Position);
                    evt.EventTimer = 0;
                    evt.IsActive = true;
                    SaveState();
                }
            }

            if (!evt.IsActive && !evt.IsCompleted)
            {
                array<Man> players = new array<Man>;
                GetGame().GetPlayers(players);

                foreach (Man p : players)
                {
                    if (!p || !p.IsAlive())
                    {
                        continue;
                    }

                    if (vector.Distance(p.GetPosition(), evt.Position) <= m_Config.ActivationRadius)
                    {
                        evt.IsActive = true;

                        string msgActive = m_Config.MsgActive;

                        if (msgActive.Contains("%1"))
                        {
                            msgActive = string.Format(msgActive, evt.LocationName);
                        }
                        else
                        {
                            msgActive = msgActive + " [" + evt.LocationName + "]";
                        }

                        SDN_SendNotificationToAll(m_Config.TitleActive, msgActive, ICON_PATH, 8.0);

                        Log("Activated by player at " + evt.LocationName);
                        evt.EventTimer = 0;
                        SaveState();
                        break;
                    }
                }

                if (!evt.IsActive && evt.LifeTime >= (m_Config.TimeToActivate * 60))
                {

                    string msgFail = m_Config.MsgFail;

                    if (msgFail.Contains("%1"))
                    {
                        msgFail = string.Format(msgFail, evt.LocationName);
                    }
                    else
                    {
                        msgFail = msgFail + " [" + evt.LocationName + "]";
                    }

                    SDN_SendNotificationToAll(m_Config.TitleFail, msgFail, ICON_PATH, 8.0);

                    CleanGroundItems(evt.Position, 10.0);

                    if (m_Config && m_Config.MarkerSettings)
                    {
                        SDN_MarkerManager.RemoveEventMarker(evt.LocationName, m_Config.MarkerSettings);
                    }

                    evt.Cleanup();
                    m_ActiveEvents.Remove(i);
                    SaveState();
                    continue;
                }
            }
            else
            {
                evt.EventTimer += delta;

                if (evt.EventTimer >= (m_Config.TimeAfterActivation * 60))
                {
                    if (!evt.IsCompleted)
                    {
                        string msgFailPost = m_Config.MsgFail;

                        if (msgFailPost.Contains("%1"))
                        {
                            msgFailPost = string.Format(msgFailPost, evt.LocationName);
                        }
                        else
                        {
                            msgFailPost = msgFailPost + " [" + evt.LocationName + "]";
                        }

                        SDN_SendNotificationToAll(m_Config.TitleFail, msgFailPost, ICON_PATH, 8.0);
                    }

                    CleanGroundItems(evt.Position, 10.0);

                    if (m_Config && m_Config.MarkerSettings)
                    {
                        SDN_MarkerManager.RemoveEventMarker(evt.LocationName, m_Config.MarkerSettings);
                    }

                    evt.Cleanup();
                    m_ActiveEvents.Remove(i);
                    SaveState();
                }
            }
        }
    }

    void SDN_SendNotificationToAll(string title, string message, string iconPath, float duration)
    {
        if (!GetGame().IsServer())
        {
            return;
        }

        if (message == "")
        {
            return;
        }

        bool soundEnabled = true;

        if (m_Config)
        {
            soundEnabled = m_Config.EnableSound;
        }

        array<Man> players = new array<Man>;
        GetGame().GetPlayers(players);

        foreach (Man player : players)
        {
            if (player && player.GetIdentity())
            {
                ScriptRPC rpc = new ScriptRPC();
                rpc.Write(title);
                rpc.Write(message);
                rpc.Write(iconPath);
                rpc.Write(duration);
                rpc.Write(soundEnabled);

                rpc.Send(player, SDN_NotificationRPC.SDN_EVENT_MSG, true, player.GetIdentity());
            }
        }

        // Dispara o Webhook do Discord em segundo plano
        SendDiscordWebhook(title, message);
    }

    void SendGlobalMessage(string msg)
    {
        // Desativado
    }
}