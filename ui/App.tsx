import React, { useEffect } from "react";
import { Sidebar } from "./components/Sidebar";
import { DuplicateDeck } from "./components/DuplicateDeck";
import { TagInspector } from "./components/TagInspector";
import { MirroringView } from "./components/MirroringView";
import { DatabaseView } from "./components/DatabaseView";
import { SettingsView } from "./components/SettingsView";
import { LogDrawer } from "./components/LogDrawer";
import { useGlobalHotkeys } from "./hooks/useHotkeys";
import { useAppStore } from "./store";

export const App: React.FC = () => {
  const activeTab = useAppStore((s) => s.activeTab);
  const checkStatus = useAppStore((s) => s.checkStatus);

  useGlobalHotkeys();

  useEffect(() => {
    checkStatus();
    const interval = setInterval(checkStatus, 3000);
    return () => clearInterval(interval);
  }, [checkStatus]);

  return (
    <div className="flex h-screen w-screen bg-[#151515] text-[#c3c2b7] overflow-hidden">
      {/* Permanent Left Sidebar (Linear/Spotify style) */}
      <Sidebar />

      {/* Main Screen Arena */}
      <main className="flex-1 flex flex-col h-full overflow-hidden relative">
        {activeTab === "duplicates" && <DuplicateDeck />}
        {activeTab === "inspector" && <TagInspector />}
        {activeTab === "mirror" && <MirroringView />}
        {activeTab === "database" && <DatabaseView />}
        {activeTab === "settings" && <SettingsView />}

        {/* Real-time Log Console Drawer */}
        <LogDrawer />
      </main>
    </div>
  );
};
