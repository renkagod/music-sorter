import React, { useEffect } from "react";
import { CopyCheck, RefreshCw, FolderCheck, HardDrive, ArrowRight, CheckCircle2 } from "lucide-react";
import { useAppStore } from "../store";

export const MirroringView: React.FC = () => {
  const mirrorProgress = useAppStore((s) => s.mirrorProgress);
  const startMirroring = useAppStore((s) => s.startMirroring);
  const fetchMirrorStatus = useAppStore((s) => s.fetchMirrorStatus);
  const settings = useAppStore((s) => s.settings);
  const fetchSettings = useAppStore((s) => s.fetchSettings);

  useEffect(() => {
    fetchSettings();
    fetchMirrorStatus();
    const interval = setInterval(() => {
      fetchMirrorStatus();
    }, 1000);
    return () => clearInterval(interval);
  }, [fetchSettings, fetchMirrorStatus]);

  const pct =
    mirrorProgress.totalTasks > 0
      ? (mirrorProgress.completedTasks / mirrorProgress.totalTasks) * 100
      : 0;

  return (
    <div className="flex-1 flex flex-col h-full bg-[#151515] p-8 overflow-y-auto space-y-8">
      {/* Header */}
      <div>
        <h1 className="text-xl font-bold text-white flex items-center space-x-2">
          <CopyCheck className="w-5 h-5 text-[#D97757]" />
          <span>Зеркалирование коллекций (FLAC ↔ MP3)</span>
        </h1>
        <p className="text-xs text-[#898781] mt-1">
          Автоматическая синхронизация: многопоточная конвертация FLAC в MP3 320kbps с копированием обложек cover.jpg и обратный перенос MP3-фоллбэков.
        </p>
      </div>

      {/* Folders Overview Card */}
      <div className="grid grid-cols-2 gap-6">
        <div className="p-5 rounded-2xl bg-[#1c1c1c] border border-[#333230] space-y-2">
          <div className="flex items-center space-x-2 text-xs font-semibold text-[#D97757]">
            <HardDrive className="w-4 h-4" />
            <span>Мастер-коллекция (FLAC)</span>
          </div>
          <div className="text-xs font-mono text-white bg-[#151515] p-2.5 rounded-lg border border-[#2a2a2a] break-all">
            {settings.flacDir || "Не настроено"}
          </div>
          <div className="text-[11px] text-[#898781]">
            Исходные Hi-Res треки с несжатыми тегами VorbisComment и Front Cover.
          </div>
        </div>

        <div className="p-5 rounded-2xl bg-[#1c1c1c] border border-[#333230] space-y-2">
          <div className="flex items-center space-x-2 text-xs font-semibold text-[#6da7ec]">
            <HardDrive className="w-4 h-4" />
            <span>Зеркало для плееров (MP3 320k)</span>
          </div>
          <div className="text-xs font-mono text-white bg-[#151515] p-2.5 rounded-lg border border-[#2a2a2a] break-all">
            {settings.mp3Dir || "Не настроено"}
          </div>
          <div className="text-[11px] text-[#898781]">
            Конвертированные 320kbps MP3 с ID3v2.3 тегами и встроенными APIC изображениями.
          </div>
        </div>
      </div>

      {/* Progress & Trigger Card */}
      <div className="p-6 rounded-2xl bg-[#1c1c1c] border border-[#333230] space-y-6">
        <div className="flex items-center justify-between">
          <div className="space-y-1">
            <h2 className="text-sm font-semibold text-white">Статус синхронизации</h2>
            <div className="text-xs text-[#898781]">
              {mirrorProgress.isRunning
                ? "Выполняется конвертация и синхронизация в фоне..."
                : "Готово к запуску или синхронизировано."}
            </div>
          </div>

          <button
            onClick={startMirroring}
            disabled={mirrorProgress.isRunning}
            className="flex items-center space-x-2 px-6 py-2.5 rounded-xl bg-[#D97757] hover:bg-[#e58a6d] text-white font-semibold text-xs transition-all shadow-lg disabled:opacity-50"
          >
            <RefreshCw className={`w-4 h-4 ${mirrorProgress.isRunning ? "animate-spin" : ""}`} />
            <span>{mirrorProgress.isRunning ? "Синхронизация..." : "Запустить зеркалирование"}</span>
          </button>
        </div>

        {/* Progress Bar */}
        <div className="space-y-2">
          <div className="flex justify-between text-xs text-[#c3c2b7]">
            <span>Прогресс: {mirrorProgress.completedTasks} / {mirrorProgress.totalTasks} файлов</span>
            <span className="font-semibold text-white">{pct.toFixed(1)}%</span>
          </div>
          <div className="w-full h-2.5 bg-[#151515] rounded-full overflow-hidden border border-[#2e2e2e]">
            <div
              className="bg-[#D97757] h-full transition-all duration-300 rounded-full shadow-[0_0_10px_rgba(217,119,87,0.5)]"
              style={{ width: `${pct}%` }}
            />
          </div>
        </div>

        {/* Metrics Grid */}
        <div className="grid grid-cols-3 gap-4 pt-2">
          <div className="p-3 bg-[#171717] rounded-xl border border-[#2a2a2a] text-center">
            <div className="text-lg font-bold text-white">{mirrorProgress.completedTasks}</div>
            <div className="text-[11px] text-[#898781]">Конвертировано в MP3</div>
          </div>
          <div className="p-3 bg-[#171717] rounded-xl border border-[#2a2a2a] text-center">
            <div className="text-lg font-bold text-emerald-400">{mirrorProgress.copiedFallbacks}</div>
            <div className="text-[11px] text-[#898781]">MP3 фоллбэков в FLAC</div>
          </div>
          <div className="p-3 bg-[#171717] rounded-xl border border-[#2a2a2a] text-center">
            <div className="text-lg font-bold text-[#6da7ec]">{mirrorProgress.createdFolders}</div>
            <div className="text-[11px] text-[#898781]">Папок создано</div>
          </div>
        </div>
      </div>
    </div>
  );
};
