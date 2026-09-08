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
} from "lucide-react";
import { useAppStore } from "../store";

export const Sidebar: React.FC = () => {
  const activeTab = useAppStore((s) => s.activeTab);
  const setActiveTab = useAppStore((s) => s.setActiveTab);
  const coreConnected = useAppStore((s) => s.coreConnected);
  const stats = useAppStore((s) => s.systemStats);
  const isLogsOpen = useAppStore((s) => s.isLogsOpen);
  const toggleLogs = useAppStore((s) => s.toggleLogs);

  const navItems = [
    {
      id: "duplicates" as const,
      label: "1. Дубликаты (A/B)",
      icon: Layers,
      badge: stats.duplicatesCount > 0 ? stats.duplicatesCount : null,
      badgeColor: "bg-[#D97757] text-white",
    },
    {
      id: "inspector" as const,
      label: "2. Инспектор тегов",
      icon: Tags,
      badge: stats.unresolvedTagsCount > 0 ? stats.unresolvedTagsCount : null,
      badgeColor: "bg-[#6da7ec] text-[#151515] font-bold",
    },
    {
      id: "mirror" as const,
      label: "3. Зеркалирование",
      icon: CopyCheck,
      badge: stats.isMirroring ? "Sync" : null,
      badgeColor: "bg-emerald-500 text-black font-semibold",
    },
    {
      id: "database" as const,
      label: "4. База данных",
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
    <aside className="w-64 flex-shrink-0 flex flex-col justify-between bg-[#151515] border-r border-[#333230] select-none h-full">
      {/* Top Header & Core Status */}
      <div>
        <div className="p-4 flex items-center justify-between border-b border-[#333230]">
          <div className="flex items-center space-x-2.5">
            <div className="w-8 h-8 rounded-lg bg-[#1c1c1c] border border-[#333230] flex items-center justify-center text-[#D97757]">
              <Disc className="w-5 h-5 animate-spin" style={{ animationDuration: "12s" }} />
            </div>
            <div>
              <div className="text-sm font-semibold tracking-wide text-white">MusicSorter</div>
              <div className="text-[11px] text-[#898781]">Studio 2.0 Headless</div>
            </div>
          </div>
          <div className="flex items-center space-x-1.5 bg-[#1c1c1c] border border-[#333230] px-2 py-1 rounded-full text-[10px]">
            <span
              className={`w-2 h-2 rounded-full ${
                coreConnected
                  ? "bg-emerald-400 shadow-[0_0_8px_rgba(52,211,153,0.8)]"
                  : "bg-rose-500 animate-pulse"
              }`}
            />
            <span className={coreConnected ? "text-emerald-400 font-medium" : "text-rose-400"}>
              {coreConnected ? "Ready" : "Offline"}
            </span>
          </div>
        </div>

        {/* Navigation Items */}
        <nav className="p-3 space-y-1.5">
          <div className="px-2 py-1 text-[10px] font-semibold tracking-wider uppercase text-[#898781]">
            Этапы обработки
          </div>
          {navItems.map((item) => {
            const Icon = item.icon;
            const isActive = activeTab === item.id;
            return (
              <button
                key={item.id}
                onClick={() => setActiveTab(item.id)}
                className={`w-full flex items-center justify-between px-3 py-2 rounded-lg text-xs transition-all duration-150 ${
                  isActive
                    ? "bg-[#1c1c1c] text-white font-medium border border-[#42403c] shadow-sm"
                    : "text-[#c3c2b7] hover:bg-[#1c1c1c]/60 hover:text-white"
                }`}
              >
                <div className="flex items-center space-x-2.5">
                  <Icon
                    className={`w-4 h-4 ${
                      isActive ? "text-[#D97757]" : "text-[#898781]"
                    }`}
                  />
                  <span>{item.label}</span>
                </div>
                {item.badge && (
                  <span
                    className={`px-1.5 py-0.5 rounded text-[10px] ${item.badgeColor}`}
                  >
                    {item.badge}
                  </span>
                )}
              </button>
            );
          })}
        </nav>
      </div>

      {/* Footer / Stats & Log Terminal Trigger */}
      <div className="p-3 border-t border-[#333230] space-y-2 bg-[#171717]">
        <div className="flex items-center justify-between text-[11px] text-[#898781] px-1">
          <div className="flex items-center space-x-1.5">
            <Activity className="w-3.5 h-3.5 text-emerald-400" />
            <span>C++ Engine 64-bit</span>
          </div>
          <span className="text-[10px] text-[#898781]">127.0.0.1:8765</span>
        </div>

        <button
          onClick={toggleLogs}
          className={`w-full flex items-center justify-between px-3 py-1.5 rounded text-xs transition-colors border ${
            isLogsOpen
              ? "bg-[#D97757]/10 text-[#D97757] border-[#D97757]/40"
              : "bg-[#1c1c1c] text-[#c3c2b7] border-[#333230] hover:text-white hover:border-[#42403c]"
          }`}
        >
          <div className="flex items-center space-x-2">
            <Terminal className="w-3.5 h-3.5" />
            <span>Логи ядра</span>
          </div>
          <span className="kbd-badge">[~]</span>
        </button>
      </div>
    </aside>
  );
};
