import React, { useEffect, useRef, useState } from "react";
import { Terminal, X, ArrowDown, Trash2 } from "lucide-react";
import { useAppStore } from "../store";

export const LogDrawer: React.FC = () => {
  const isLogsOpen = useAppStore((s) => s.isLogsOpen);
  const toggleLogs = useAppStore((s) => s.toggleLogs);
  const logs = useAppStore((s) => s.logs);
  const fetchLogs = useAppStore((s) => s.fetchLogs);

  const [autoScroll, setAutoScroll] = useState(true);
  const logEndRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (isLogsOpen) {
      fetchLogs();
      const interval = setInterval(fetchLogs, 1500);
      return () => clearInterval(interval);
    }
  }, [isLogsOpen, fetchLogs]);

  useEffect(() => {
    if (autoScroll && logEndRef.current) {
      logEndRef.current.scrollIntoView({ behavior: "smooth" });
    }
  }, [logs, autoScroll]);

  if (!isLogsOpen) return null;

  return (
    <div className="fixed inset-x-0 bottom-0 h-72 bg-[#171717] border-t border-[#333230] shadow-2xl flex flex-col z-50 animate-in slide-in-from-bottom duration-200">
      {/* Header */}
      <div className="h-10 px-4 bg-[#1c1c1c] border-b border-[#333230] flex items-center justify-between">
        <div className="flex items-center space-x-2 text-xs font-semibold text-white">
          <Terminal className="w-3.5 h-3.5 text-[#D97757]" />
          <span>Консоль логов C++ ядра (music-sorter-core)</span>
          <span className="text-[10px] text-[#898781]">({logs.length} строк)</span>
        </div>

        <div className="flex items-center space-x-2">
          <button
            onClick={() => setAutoScroll(!autoScroll)}
            className={`px-2 py-0.5 rounded text-[11px] border transition-colors flex items-center space-x-1 ${
              autoScroll
                ? "bg-[#D97757]/20 text-[#D97757] border-[#D97757]/40"
                : "bg-[#222] text-[#898781] border-[#333230]"
            }`}
          >
            <ArrowDown className="w-3 h-3" />
            <span>Автопрокрутка</span>
          </button>

          <button
            onClick={toggleLogs}
            className="p-1 text-[#898781] hover:text-white rounded"
          >
            <X className="w-4 h-4" />
          </button>
        </div>
      </div>

      {/* Log Output Body */}
      <div className="flex-1 p-3 overflow-y-auto font-mono text-[11px] leading-relaxed text-[#c3c2b7] bg-[#121212] select-text">
        {logs.length === 0 ? (
          <div className="text-[#898781] italic">Ожидание логов ядра...</div>
        ) : (
          logs.map((line, idx) => {
            let color = "text-[#c3c2b7]";
            if (line.includes("[ERROR]")) color = "text-rose-400 font-semibold";
            else if (line.includes("[WARN]")) color = "text-amber-400";
            else if (line.includes("[MATCHED]") || line.includes("[TAGS EMBEDDED]"))
              color = "text-emerald-400";
            else if (line.includes("[RESOLVE]") || line.includes("[SEARCH]"))
              color = "text-[#6da7ec]";

            return (
              <div key={idx} className={`${color} hover:bg-[#1a1a1a] px-1 rounded`}>
                {line}
              </div>
            );
          })
        )}
        <div ref={logEndRef} />
      </div>
    </div>
  );
};
