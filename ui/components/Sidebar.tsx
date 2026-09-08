import React from "react";
import {
  Layers,
  Tags,
  CopyCheck,
  Database,
  Settings as SettingsIcon,
  Terminal,
  Activity,
  Disc,
  PanelLeftClose,
  PanelLeftOpen,
} from "lucide-react";
import { useAppStore } from "../store";

export const Sidebar: React.FC = () => {
  const activeTab = useAppStore((s) => s.activeTab);
  const setActiveTab = useAppStore((s) => s.setActiveTab);
  const coreConnected = useAppStore((s) => s.coreConnected);
  const stats = useAppStore((s) => s.systemStats);
  const isLogsOpen = useAppStore((s) => s.isLogsOpen);
  const toggleLogs = useAppStore((s) => s.toggleLogs);
  const isSidebarCollapsed = useAppStore((s) => s.isSidebarCollapsed);
  const toggleSidebar = useAppStore((s) => s.toggleSidebar);

  const navItems = [
    {
      id: "duplicates" as const,
      label: "Дубликаты",
      icon: Layers,
      badge: stats.duplicatesCount > 0 ? stats.duplicatesCount : null,
      badgeColor: "bg-[#D97757] text-white",
    },
    {
      id: "inspector" as const,
      label: "Теги",
      icon: Tags,
      badge: stats.unresolvedTagsCount > 0 ? stats.unresolvedTagsCount : null,
      badgeColor: "bg-[#6da7ec] text-[#151515] font-bold",
    },
    {
      id: "mirror" as const,
      label: "Синхронизация",
      icon: CopyCheck,
      badge: stats.isMirroring ? "Sync" : null,
      badgeColor: "bg-emerald-500 text-black font-semibold",
    },
    {
      id: "database" as const,
      label: "База треков",
      icon: Database,
      badge: stats.totalTracks > 0 ? `${stats.downloadedTracks}/${stats.totalTracks}` : null,
      badgeColor: "bg-[#2a2a2a] text-[#c3c2b7]",
    },
    {
      id: "settings" as const,
      label: "Настройки",
      icon: SettingsIcon,
      badge: null,
      badgeColor: "",
    },
  ];

  return (
    <aside
      className={`${
        isSidebarCollapsed ? "w-16" : "w-60"
      } flex-shrink-0 flex flex-col justify-between bg-[#151515] border-r border-[#333230] select-none h-full transition-all duration-200 ease-in-out`}
    >
      {/* Top Header & Core Status */}
      <div>
        <div
          className={`p-3.5 flex items-center border-b border-[#333230] ${
            isSidebarCollapsed ? "justify-center flex-col space-y-2" : "justify-between"
          }`}
        >
          <div className="flex items-center space-x-2.5 overflow-hidden">
            <div
              className="w-8 h-8 rounded-lg bg-[#1c1c1c] border border-[#333230] flex items-center justify-center text-[#D97757] flex-shrink-0"
              title="MusicSorter"
            >
              <Disc className="w-5 h-5 animate-spin" style={{ animationDuration: "12s" }} />
            </div>
            {!isSidebarCollapsed && (
              <div className="min-w-0">
                <div className="text-sm font-semibold tracking-wide text-white truncate">
                  MusicSorter
                </div>
                <div className="text-[10px] text-[#898781] truncate">Сортировка фонотеки</div>
              </div>
            )}
          </div>

          <div className="flex items-center space-x-1">
            {!isSidebarCollapsed && (
              <div
                className="flex items-center space-x-1.5 bg-[#1c1c1c] border border-[#333230] px-2 py-0.5 rounded-full text-[10px]"
                title={coreConnected ? "Ядро подключено" : "Ядро отключено"}
              >
                <span
                  className={`w-2 h-2 rounded-full ${
                    coreConnected
                      ? "bg-emerald-400 shadow-[0_0_8px_rgba(52,211,153,0.8)]"
                      : "bg-rose-500 animate-pulse"
                  }`}
                />
                <span className={coreConnected ? "text-emerald-400 font-medium" : "text-rose-400"}>
                  {coreConnected ? "В сети" : "Офлайн"}
                </span>
              </div>
            )}

            <button
              onClick={toggleSidebar}
              className="p-1.5 text-[#898781] hover:text-white rounded-lg hover:bg-[#222] transition-colors"
              title={isSidebarCollapsed ? "Развернуть меню" : "Свернуть меню"}
            >
              {isSidebarCollapsed ? (
                <PanelLeftOpen className="w-4 h-4" />
              ) : (
                <PanelLeftClose className="w-4 h-4" />
              )}
            </button>
          </div>
        </div>

        {/* Navigation Items */}
        <nav className="p-2 space-y-1">
          {!isSidebarCollapsed && (
            <div className="px-2 py-1 text-[10px] font-semibold tracking-wider uppercase text-[#898781]">
              Разделы
            </div>
          )}
          {navItems.map((item) => {
            const Icon = item.icon;
            const isActive = activeTab === item.id;
            return (
              <button
                key={item.id}
                onClick={() => setActiveTab(item.id)}
                title={isSidebarCollapsed ? item.label : undefined}
                className={`w-full flex items-center ${
                  isSidebarCollapsed ? "justify-center p-2.5" : "justify-between px-3 py-2"
                } rounded-lg text-xs transition-all duration-150 relative ${
                  isActive
                    ? "bg-[#1c1c1c] text-white font-medium border border-[#42403c] shadow-sm"
                    : "text-[#c3c2b7] hover:bg-[#1c1c1c]/60 hover:text-white"
                }`}
              >
                <div className={`flex items-center ${isSidebarCollapsed ? "" : "space-x-2.5"}`}>
                  <Icon
                    className={`w-4 h-4 flex-shrink-0 ${
                      isActive ? "text-[#D97757]" : "text-[#898781]"
                    }`}
                  />
                  {!isSidebarCollapsed && <span className="truncate">{item.label}</span>}
                </div>

                {item.badge && !isSidebarCollapsed && (
                  <span className={`px-1.5 py-0.5 rounded text-[10px] ${item.badgeColor}`}>
                    {item.badge}
                  </span>
                )}

                {item.badge && isSidebarCollapsed && (
                  <span
                    className={`absolute top-1.5 right-1.5 w-2 h-2 rounded-full ${
                      item.id === "duplicates"
                        ? "bg-[#D97757]"
                        : item.id === "inspector"
                        ? "bg-[#6da7ec]"
                        : "bg-emerald-400"
                    }`}
                  />
                )}
              </button>
            );
          })}
        </nav>
      </div>

      {/* Footer / Stats & Log Terminal Trigger */}
      <div className="p-2 border-t border-[#333230] space-y-1.5 bg-[#171717]">
        {!isSidebarCollapsed ? (
          <div className="flex items-center justify-between text-[11px] text-[#898781] px-1">
            <div className="flex items-center space-x-1.5">
              <Activity className="w-3.5 h-3.5 text-emerald-400" />
              <span>Движок C++</span>
            </div>
            <span className="text-[10px] text-[#898781]">127.0.0.1:8765</span>
          </div>
        ) : (
          <div className="flex justify-center py-1" title="Ядро: 127.0.0.1:8765">
            <span
              className={`w-2 h-2 rounded-full ${
                coreConnected ? "bg-emerald-400" : "bg-rose-500 animate-pulse"
              }`}
            />
          </div>
        )}

        <button
          onClick={toggleLogs}
          title={isSidebarCollapsed ? "Журнал событий [~]" : undefined}
          className={`w-full flex items-center ${
            isSidebarCollapsed ? "justify-center p-2" : "justify-between px-3 py-1.5"
          } rounded text-xs transition-colors border ${
            isLogsOpen
              ? "bg-[#D97757]/10 text-[#D97757] border-[#D97757]/40"
              : "bg-[#1c1c1c] text-[#c3c2b7] border-[#333230] hover:text-white hover:border-[#42403c]"
          }`}
        >
          <div className="flex items-center space-x-2">
            <Terminal className="w-3.5 h-3.5 flex-shrink-0" />
            {!isSidebarCollapsed && <span>Журнал</span>}
          </div>
          {!isSidebarCollapsed && <span className="kbd-badge">[~]</span>}
        </button>
      </div>
    </aside>
  );
};
