// PASTA: 4_World
// ARQUIVO: SDN_PlayerListener.c

modded class PlayerBase
{
    override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
    {
        super.OnRPC(sender, rpc_type, ctx);

        // Segurança 1: Apenas Client
        if (!GetGame().IsClient()) return;

        // Segurança 2: Apenas Jogador Local
        if (GetGame().GetPlayer() != this) return;
        
        // ID RPC: 8410225
        if (rpc_type == 8410225)
        {
            string title;
            if (!ctx.Read(title)) { return; }

            string text;
            if (!ctx.Read(text)) { return; }

            string icon;
            if (!ctx.Read(icon)) { return; }

            float time;
            if (!ctx.Read(time)) { return; }

            // [SDN] Ler configuração de som enviada pelo servidor
            bool enableSound;
            if (!ctx.Read(enableSound)) { return; }

            // [SDN] Tocar Som de Notificação APENAS se ativado na config
            if (enableSound && GetGame().GetPlayer())
            {
                SEffectManager.PlaySoundOnObject("notification_SoundSet", GetGame().GetPlayer(), 0, 0.5, false);
            }

            // Chamada com assinatura INVERTIDA (Tempo primeiro) baseada no seu mod de referência
#ifdef VPPNOTIFICATIONS
            NotificationSystem.AddNotificationExtended(time, title, text, icon);
#else
            // Fallback for when NotificationSystem is not available.
            // DayZ does not have a global UI broadcast natively out of the box in this exact format,
            // so we fallback to nothing or a custom print if the user does not have the VPP mod installed.
            Print("[SDN Event] " + title + " - " + text);
#endif
        }
    }
}