#pragma once
#include <windows.h>
#include <string>

enum class Lang { Auto = 0, PT = 1, EN = 2 };

enum class StrId {
    AppName,
    NoDevicesFound,
    ClickToHide,
    HideLastKnown,
    ModeWidget,
    ModeTray,
    WidgetBackground,
    RefreshNow,
    StartWithWindows,
    Exit,
    LowBattery,
    OffNotConnected,
    Charging,
    Asleep,
    Unknown,
    LastKnown,
    BatteryAlertSubmenu,
    UpdateIntervalSubmenu,
    LanguageSubmenu,
    LangAuto,
    LangPT,
    LangEN,
    Interval30s,
    Interval60s,
    Interval2m,
    Interval5m,
    G435JieliNoTelemetry,
};

inline Lang g_lang = Lang::Auto;

inline bool IsPortuguese() {
    if (g_lang == Lang::PT) return true;
    if (g_lang == Lang::EN) return false;
    LANGID uiLang = GetUserDefaultUILanguage();
    return PRIMARYLANGID(uiLang) == LANG_PORTUGUESE;
}

inline const wchar_t* Tr(StrId id) {
    bool pt = IsPortuguese();
    switch (id) {
        case StrId::AppName:
            return L"Wireless Battery";
        case StrId::NoDevicesFound:
            return pt ? L"Nenhum dispositivo com info de bateria encontrado"
                      : L"No devices with battery info found";
        case StrId::ClickToHide:
            return pt ? L"Clique em um dispositivo para exibir ou ocultar"
                      : L"Click a device to show or hide it";
        case StrId::HideLastKnown:
            return pt ? L"Ocultar dispositivos em repouso / último valor"
                      : L"Hide last known / asleep devices";
        case StrId::ModeWidget:
            return pt ? L"Exibir na barra de tarefas (ícone + porcentagem)"
                      : L"Show on the taskbar (icon + percentage)";
        case StrId::ModeTray:
            return pt ? L"Exibir como ícones na bandeja"
                      : L"Show as tray icons";
        case StrId::WidgetBackground:
            return pt ? L"Fundo escuro atrás do texto (para temas transparentes)"
                      : L"Background behind taskbar text (for transparent taskbars)";
        case StrId::RefreshNow:
            return pt ? L"Atualizar agora"
                      : L"Refresh now";
        case StrId::StartWithWindows:
            return pt ? L"Iniciar com o Windows"
                      : L"Start with Windows";
        case StrId::Exit:
            return pt ? L"Sair"
                      : L"Exit";
        case StrId::LowBattery:
            return pt ? L"Bateria fraca"
                      : L"Low battery";
        case StrId::OffNotConnected:
            return pt ? L"desligado / desconectado"
                      : L"off / not connected";
        case StrId::Charging:
            return pt ? L"carregando"
                      : L"charging";
        case StrId::Asleep:
            return pt ? L"em repouso"
                      : L"asleep";
        case StrId::Unknown:
            return pt ? L"desconhecido"
                      : L"unknown";
        case StrId::LastKnown:
            return pt ? L"(último valor)"
                      : L"(last known)";
        case StrId::BatteryAlertSubmenu:
            return pt ? L"Alerta de bateria fraca"
                      : L"Low battery alert";
        case StrId::UpdateIntervalSubmenu:
            return pt ? L"Intervalo de atualização"
                      : L"Update interval";
        case StrId::LanguageSubmenu:
            return pt ? L"Idioma / Language"
                      : L"Language / Idioma";
        case StrId::LangAuto:
            return pt ? L"Automático (Windows)"
                      : L"Automatic (Windows)";
        case StrId::LangPT:
            return L"Português (Brasil)";
        case StrId::LangEN:
            return L"English";
        case StrId::Interval30s:
            return L"30s";
        case StrId::Interval60s:
            return pt ? L"60s (Padrão)" : L"60s (Default)";
        case StrId::Interval2m:
            return L"2 min";
        case StrId::Interval5m:
            return L"5 min";
        case StrId::G435JieliNoTelemetry:
            return pt ? L"receptor Jieli 4C4A:4155 — sem telemetria USB"
                      : L"Jieli 4C4A:4155 receiver — no USB telemetry";
        default:
            return L"";
    }
}

inline std::wstring FormatLowBattery(const std::wstring& name, int pct) {
    if (IsPortuguese()) {
        return name + L" está em " + std::to_wstring(pct) + L"%";
    }
    return name + L" is at " + std::to_wstring(pct) + L"%";
}
