
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
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SV98", "0.9102 -0.7076 2.2514", "-2.3318 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SV98", "0.6302 -0.7076 2.2514", "-2.3318 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SV98", "0.9002 -0.7076 2.1514", "-2.3318 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SV98", "0.6202 -0.7076 2.1514", "-2.3318 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SV98", "0.8902 -0.7076 2.0514", "-2.3318 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SV98", "0.6102 -0.7076 2.0514", "-2.3318 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SDN_P_smoke_small_barrel_01", "3.4502 -1.5036 -5.0486", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_556x45_20Rnd", "0.0502 -1.0946 1.7514", "9.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_556x45_20Rnd", "-0.0398 -1.0826 1.4514", "22.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_556x45_20Rnd", "-7.6498 -2.3606 1.4514", "-60.2058 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_Misc_Gunrack", "0.6502 -0.7476 1.8514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SVD", "0.6002 -0.7096 1.7514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SVD", "0.3302 -0.7096 1.6514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SVD", "0.5902 -0.7096 1.6514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SVD", "0.3202 -0.7096 1.5514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("M14", "0.8802 -0.6486 1.5514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("M14", "0.8702 -0.6486 1.6514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("M14", "0.8802 -0.6486 1.7514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_Wreck_Decal_Small2_NoLC", "0.9702 -2.4596 4.5514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Offroad_02_Wheel_Ruined", "2.4402 -2.0946 3.7514", "-18.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SDN_P_smoke_medium_house_01", "3.6702 -1.6196 -8.2486", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_762x39", "0.2102 -1.5196 0.7514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_12gaPellets", "-0.5698 0.1484 2.6514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_12gaPellets", "-0.7798 0.1454 2.6514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("FireplaceLight", "1.1002 -0.9046 -8.3486", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("FireplaceLight", "3.3702 -1.3766 -5.0486", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Land_Wreck_Truck02_Green", "0.4202 -2.4566 -5.1486", "-102.9060 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_556x45", "0.1302 -1.0936 1.0514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_556x45", "0.1202 -1.0766 0.6514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_556x45", "0.1202 -1.0766 0.6514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Land_Wreck_LR_Armed_TK", "2.3302 -2.5506 5.4514", "-27.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Land_Wreck_Pickup01_Armed_TK", "2.0602 -1.4316 -9.8486", "-156.3410 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_9x39AP", "-0.2598 -1.0766 1.5514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_9x39AP", "-0.8298 -1.0536 1.7514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_Wreck_Decal_Small1_NoLC", "3.9502 -2.4576 -3.9486", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_Wreck_Decal_Small1_NoLC", "3.6502 -2.4476 -10.1486", "18.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_762x39_20Rnd", "-0.1498 -1.0396 1.5514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_762x39_20Rnd", "-0.4798 -0.2316 0.6514", "-56.2058 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_762x39_20Rnd", "-0.6598 -0.2316 0.4514", "-56.2058 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_762x39_20Rnd", "3.3102 -2.3036 5.7514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SVD_Wooden", "0.6002 -0.7246 1.5514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SVD_Wooden", "0.3302 -0.7246 1.4514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_00buck_10rnd", "-0.8398 0.1924 2.5514", "71.0535 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_00buck_10rnd", "-0.7398 -0.2326 0.4514", "-56.2058 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_00buck_10rnd", "-0.8798 0.1914 2.5514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_9x19", "-0.2898 -1.0726 2.4514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_9x19_25rnd", "-0.5098 -1.0266 1.0514", "18.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_9x19_25rnd", "-0.1798 -1.0216 1.6514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SDN_P_smoke_bonfire", "2.5002 -1.4986 5.2514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_Furniture_PaperBox_01_small_closed", "-0.6698 -0.2726 2.5514", "0.3114 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_Furniture_PaperBox_01_small_closed", "-0.6698 -0.6846 2.5514", "0.3114 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_Furniture_PaperBox_01_small_closed", "-0.6698 -1.0966 2.5514", "0.3114 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SDN_P_fire_small_camp_01_start", "3.6702 -1.6166 -8.2486", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_40mm_Explosive", "-0.0198 -1.0316 0.6514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_308Win", "-0.1098 -1.0646 2.2514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_308Win", "-0.6198 0.1424 2.5514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_308Win", "0.4902 -1.0646 1.8514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SDN_P_fire_tirepile", "1.2602 -0.6236 -8.0486", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("ShippingContainerKeys_Blue", "23.0202 -2.4266 11.4514", "-145.1820 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_545x39_20Rnd", "-0.7398 -1.0366 1.7514", "-172.8750 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_545x39_20Rnd", "-0.6498 0.1454 2.3514", "7.1250 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_545x39_20Rnd", "-0.4398 0.1514 2.5514", "-172.8750 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("SDN_P_fire_medium_oven_01", "3.4702 -1.4196 -5.0486", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_762x54_20Rnd", "-0.4198 -0.2776 0.7514", "-56.2058 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_762x54_20Rnd", "-0.4798 0.1794 2.5514", "14.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("AmmoBox_762x54_20Rnd", "0.0602 -1.0636 1.6514", "14.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_762x54", "-0.5398 -0.2816 0.6514", "-56.2058 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Ammo_762x54", "-0.7398 0.1414 2.4514", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Truck_01_Wheel_Ruined", "0.2302 -2.3366 5.0514", "3.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Truck_01_WheelDouble_Ruined", "-0.2898 -2.1836 4.5514", "135.8900 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_ammoboxes_single", "-0.5998 0.4514 -2.1486", "89.0478 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_ammoboxes_single", "-0.6098 -0.6126 -1.9486", "91.2339 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_ammoboxes_single", "-0.5898 -0.2586 -1.9486", "89.0478 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_ammoboxes_single", "-0.4498 0.9424 -2.1486", "89.0500 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_ammoboxes_single", "-0.6198 0.6274 -0.2486", "-88.9770 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_ammoboxes_single", "-0.5898 0.0964 -1.9486", "89.0478 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_ammoboxes_single", "0.2402 -0.5966 -2.0486", "89.6686 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_Wreck_Trailer_Single_Container", "-0.0398 -2.4556 -0.8486", "0.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("Land_Wreck_Uaz", "3.5402 -1.6966 -3.6486", "3.0000 0.0000 0.0000"));
        m_SceneTemplate.Insert(new SDN_SceneObjectTemplate("StaticObj_Wreck_Decal_Small2", "1.3202 3.4184 -7.8486", "0.0000 0.0000 0.0000"));
    }

    void LoadConfig()
    {
        if (FileExist(CONFIG_PATH))
        {
            JsonFileLoader<SDN_ContainerEventConfig>.JsonLoadFile(CONFIG_PATH, m_Config);

            // Force legacy configs to update the loot box so it works dynamically
            if (m_Config && m_Config.LootContainerObject == "CONTAINER_Chest")
            {
                m_Config.LootContainerObject = "StorageBox_Wooden";
                JsonFileLoader<SDN_ContainerEventConfig>.JsonSaveFile(CONFIG_PATH, m_Config);
                Log("Updated legacy config: Loot box changed to StorageBox_Wooden.");
            }
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

            // Since GetSurfaceNormal is not available directly on GetGame(), we'll use a safer engine method
            // or simply snap it to surface and let DayZ handle it.
            // Using PlaceOnSurface() is the safest native way to ensure it sits on the terrain without floating.
            vector finalOri = "0 0 0";
            finalOri[0] = Math.RandomFloat(0, 360);

            containerObj.SetPosition(spawnPos);
            containerObj.SetOrientation(finalOri);
            containerObj.PlaceOnSurface();

            SetIDSafe(containerObj, eventID);
        }

        newEvent.Container = containerObj;
        UpdateEventSmoke(newEvent, 100.0);

        EntityAI crate = null;

        string chestName = m_Config.LootContainerObject;
        if (chestName == "") chestName = "StorageBox_Wooden";

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
            // Spawn the loot crate based on the user's exact Editor layout offset
// Offset extracted directly from the XML exported by the DayZ Editor
            // The DayZ editor sets the container origin in the center, but in-game it might be different.
            // We use ModelToWorld which is foolproof for local offsets.
            vector crateOffsetPos = "-0.5798 -1.0846 -0.0486";
            vector crateOffsetOri = "-90.9489 0 0";

            // Convert local offset to world space perfectly natively
            vector crateWorldPos = containerObj.ModelToWorld(crateOffsetPos);

            vector crateWorldOri = containerObj.GetOrientation();
            crateWorldOri[0] = crateWorldOri[0] + crateOffsetOri[0];

            crate = EntityAI.Cast(GetGame().CreateObjectEx(chestName, crateWorldPos, ECE_NOLIFETIME | ECE_SETUP));
            if (crate)
            {
                crate.SetPosition(crateWorldPos);
                crate.SetOrientation(crateWorldOri);
                crate.Update();

                // Freeze the crate so it doesn't fall through the container floor if physics glitch
                dBodyActive(crate, ActiveState.INACTIVE);

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
        if (!isRestoration) SpawnSceneObjects(newEvent, containerObj.GetPosition(), containerObj.GetOrientation(), containerObj);

        m_ActiveEvents.Insert(newEvent);

        if (m_Config && m_Config.MarkerSettings)
        {
            SDN_MarkerManager.AddEventMarker(loc.Name, spawnPos, m_Config.MarkerSettings);
        }

        Log("Event Active and Linked at " + loc.Name + " [ID: " + eventID + "]");
    }


    void SpawnSceneObjects(ActiveEventInstance evt, vector basePos, vector baseOri, EntityAI containerObj)
    {
        if (!m_SceneTemplate || !containerObj) return;

        foreach (SDN_SceneObjectTemplate tmpl : m_SceneTemplate)
        {
            vector localPos = tmpl.OffsetPos;

            // Use native Enfusion engine ModelToWorld for perfect local-to-global mapping
            vector worldPos = containerObj.ModelToWorld(localPos);

            vector worldOri = baseOri;
            worldOri[0] = worldOri[0] + tmpl.OffsetOri[0];

            Object obj = null;
            if (tmpl.ClassName.Contains(".p3d"))
            {
                obj = GetGame().CreateStaticObjectUsingP3D(tmpl.ClassName, worldPos, worldOri, 1.0, false);
            }
            else
            {
                obj = GetGame().CreateObjectEx(tmpl.ClassName, worldPos, ECE_NOLIFETIME | ECE_SETUP);
            }

            if (obj)
            {
                if (!tmpl.ClassName.Contains(".p3d"))
                {
                    obj.SetPosition(worldPos);
                    obj.SetOrientation(worldOri);
                }
                obj.Update();

                // Disable physics interaction for decorative items (cars, ammo boxes) so they act as pure static scene
                if (obj.IsInherited(ItemBase))
                {
                    ItemBase ib = ItemBase.Cast(obj);
                    ib.SetTakeable(false);
                    dBodyActive(obj, ActiveState.INACTIVE);
                }

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