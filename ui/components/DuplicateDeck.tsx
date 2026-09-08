import React, { useEffect } from "react";
import {
  Play,
  Pause,
  Volume2,
  VolumeX,
  Radio,
  CheckCircle2,
  Trash2,
  SkipForward,
  Layers,
  Sparkles,
  FileAudio,
} from "lucide-react";
import { useAppStore } from "../store";

export const DuplicateDeck: React.FC = () => {
  const duplicatePairs = useAppStore((s) => s.duplicatePairs);
  const activePairIndex = useAppStore((s) => s.activePairIndex);
  const setActivePairIndex = useAppStore((s) => s.setActivePairIndex);
  const resolveDuplicate = useAppStore((s) => s.resolveDuplicate);
  const startDuplicateScan = useAppStore((s) => s.startDuplicateScan);
  const fetchDuplicates = useAppStore((s) => s.fetchDuplicates);
  const stats = useAppStore((s) => s.systemStats);

  const audioStatus = useAppStore((s) => s.audioStatus);
  const audioPlay = useAppStore((s) => s.audioPlay);
  const audioPause = useAppStore((s) => s.audioPause);
  const audioToggle = useAppStore((s) => s.audioToggle);
  const audioSwitchChannel = useAppStore((s) => s.audioSwitchChannel);
  const audioSeek = useAppStore((s) => s.audioSeek);
  const audioSetVolume = useAppStore((s) => s.audioSetVolume);
  const waveformA = useAppStore((s) => s.waveformA);
  const waveformB = useAppStore((s) => s.waveformB);

  useEffect(() => {
    fetchDuplicates();
    const interval = setInterval(() => {
      useAppStore.getState().pollAudioStatus();
    }, 250);
    return () => clearInterval(interval);
  }, [fetchDuplicates]);

  const currentPair = duplicatePairs[activePairIndex];

  const formatTime = (sec: number) => {
    const m = Math.floor(sec / 60);
    const s = Math.floor(sec % 60);
    return `${m.toString().padStart(2, "0")}:${s.toString().padStart(2, "0")}`;
  };

  const handleWaveformClick = (e: React.MouseEvent<HTMLDivElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const clickX = e.clientX - rect.left;
    const pct = Math.max(0, Math.min(1, clickX / rect.width));
    audioSeek(pct);
  };

  if (duplicatePairs.length === 0) {
    return (
      <div className="flex-1 flex flex-col items-center justify-center p-8 text-center bg-[#151515]">
        <div className="w-16 h-16 rounded-2xl bg-[#1c1c1c] border border-[#333230] flex items-center justify-center mb-4 text-[#D97757]">
          <Layers className="w-8 h-8" />
        </div>
        <h2 className="text-xl font-semibold text-white mb-2">Дубликаты не найдены</h2>
        <p className="text-sm text-[#898781] max-w-md mb-6 leading-relaxed">
          Папка TO SORT не содержит идентичных аудиофайлов или сканирование ещё не запускалось.
        </p>
        <button
          onClick={startDuplicateScan}
          disabled={stats.isDuplicateScanning}
          className="flex items-center space-x-2 bg-[#D97757] hover:bg-[#e58a6d] text-white px-5 py-2.5 rounded-lg text-sm font-medium transition-colors shadow-lg disabled:opacity-50"
        >
          <Sparkles className="w-4 h-4" />
          <span>{stats.isDuplicateScanning ? "Идёт акустический поиск..." : "Запустить поиск дубликатов"}</span>
        </button>
      </div>
    );
  }

  const currentPercent =
    audioStatus.duration > 0
      ? (audioStatus.currentTime / audioStatus.duration) * 100
      : 0;

  return (
    <div className="flex-1 flex flex-col h-full bg-[#151515] overflow-hidden">
      {/* Top Header / Counter Bar */}
      <div className="h-14 px-6 border-b border-[#333230] flex items-center justify-between bg-[#191919] flex-shrink-0">
        <div className="flex items-center space-x-3">
          <span className="text-sm font-medium text-white">
            Пара {activePairIndex + 1} из {duplicatePairs.length}
          </span>
          <span className="text-xs bg-[#242424] text-[#c3c2b7] px-2 py-0.5 rounded border border-[#333230]">
            Сходство: {(currentPair.similarity * 100).toFixed(1)}%
          </span>
          {currentPair.offset !== 0 && (
            <span className="text-xs text-[#898781]">
              Сдвиг: {currentPair.offset} фр.
            </span>
          )}
        </div>

        {/* Pair Pagination */}
        <div className="flex items-center space-x-2">
          <button
            onClick={() => setActivePairIndex(Math.max(0, activePairIndex - 1))}
            disabled={activePairIndex === 0}
            className="px-2.5 py-1 text-xs rounded bg-[#222] border border-[#333230] text-[#c3c2b7] hover:text-white disabled:opacity-40"
          >
            ← Предыдущая
          </button>
          <button
            onClick={() =>
              setActivePairIndex(
                Math.min(duplicatePairs.length - 1, activePairIndex + 1)
              )
            }
            disabled={activePairIndex >= duplicatePairs.length - 1}
            className="px-2.5 py-1 text-xs rounded bg-[#222] border border-[#333230] text-[#c3c2b7] hover:text-white disabled:opacity-40"
          >
            Следующая →
          </button>
        </div>
      </div>

      {/* Main Dual Deck Arena */}
      <div className="flex-1 p-6 overflow-y-auto space-y-6">
        {/* Instant A/B Toggle Switcher */}
        <div className="flex items-center justify-center">
          <div className="inline-flex items-center bg-[#1c1c1c] p-1.5 rounded-xl border border-[#333230] shadow-inner space-x-2">
            <button
              onClick={() => audioSwitchChannel("a")}
              className={`flex items-center space-x-2 px-6 py-2.5 rounded-lg text-sm font-semibold transition-all ${
                audioStatus.activeChannel === "a"
                  ? "bg-[#D97757] text-white shadow-md"
                  : "text-[#898781] hover:text-white"
              }`}
            >
              <Radio className="w-4 h-4" />
              <span>ДЕКА A (FLAC / Основной)</span>
              <span className="kbd-badge">[Ctrl+A]</span>
            </button>
            <button
              onClick={() => audioSwitchChannel("b")}
              className={`flex items-center space-x-2 px-6 py-2.5 rounded-lg text-sm font-semibold transition-all ${
                audioStatus.activeChannel === "b"
                  ? "bg-[#6da7ec] text-[#151515] shadow-md"
                  : "text-[#898781] hover:text-white"
              }`}
            >
              <Radio className="w-4 h-4" />
              <span>ДЕКА B (MP3 / Дубликат)</span>
              <span className="kbd-badge">[Ctrl+B]</span>
            </button>
          </div>
        </div>

        {/* Two Decks Grid */}
        <div className="grid grid-cols-2 gap-6">
          {/* Deck A Card */}
          <div
            className={`p-5 rounded-2xl bg-[#1c1c1c] border transition-all ${
              audioStatus.activeChannel === "a"
                ? "border-[#D97757] shadow-[0_0_20px_rgba(217,119,87,0.15)]"
                : "border-[#333230] opacity-80"
            }`}
          >
            <div className="flex items-center justify-between mb-3">
              <div className="flex items-center space-x-2">
                <span className="w-2.5 h-2.5 rounded-full bg-[#D97757]" />
                <span className="text-xs font-bold uppercase tracking-wider text-white">
                  Дека A
                </span>
                <span className="text-[11px] px-1.5 py-0.5 rounded bg-[#262626] text-[#D97757] border border-[#3e3b38]">
                  {currentPair.extA.toUpperCase()}
                </span>
              </div>
              <span className="text-xs text-[#898781] font-mono">
                {formatTime(currentPair.durA)}
              </span>
            </div>

            <div className="text-sm font-medium text-white truncate mb-1" title={currentPair.relA}>
              {currentPair.relA}
            </div>
            <div className="text-xs text-[#898781] truncate mb-4 font-mono">
              {currentPair.trackA_path}
            </div>

            {/* Waveform A */}
            <div
              onClick={handleWaveformClick}
              className="h-20 bg-[#151515] rounded-xl border border-[#333230] p-2 flex items-center justify-between cursor-pointer relative overflow-hidden group"
            >
              {/* Playhead line */}
              <div
                className="absolute top-0 bottom-0 w-0.5 bg-white z-10 pointer-events-none"
                style={{ left: `${currentPercent}%` }}
              />
              <div
                className="absolute top-0 bottom-0 left-0 bg-[#D97757]/10 pointer-events-none"
                style={{ width: `${currentPercent}%` }}
              />

              <div className="flex items-center justify-between w-full h-full space-x-1">
                {waveformA.length > 0
                  ? waveformA.map((val, i) => (
                      <div
                        key={i}
                        className={`flex-1 rounded-full transition-all ${
                          i / waveformA.length <= currentPercent / 100
                            ? "bg-[#D97757]"
                            : "bg-[#42403c] group-hover:bg-[#555]"
                        }`}
                        style={{ height: `${Math.max(8, val * 100)}%` }}
                      />
                    ))
                  : Array.from({ length: 60 }).map((_, i) => (
                      <div
                        key={i}
                        className="flex-1 bg-[#333230] rounded-full"
                        style={{ height: `${20 + (i % 5) * 15}%` }}
                      />
                    ))}
              </div>
            </div>
          </div>

          {/* Deck B Card */}
          <div
            className={`p-5 rounded-2xl bg-[#1c1c1c] border transition-all ${
              audioStatus.activeChannel === "b"
                ? "border-[#6da7ec] shadow-[0_0_20px_rgba(109,167,236,0.15)]"
                : "border-[#333230] opacity-80"
            }`}
          >
            <div className="flex items-center justify-between mb-3">
              <div className="flex items-center space-x-2">
                <span className="w-2.5 h-2.5 rounded-full bg-[#6da7ec]" />
                <span className="text-xs font-bold uppercase tracking-wider text-white">
                  Дека B
                </span>
                <span className="text-[11px] px-1.5 py-0.5 rounded bg-[#262626] text-[#6da7ec] border border-[#2b3a4a]">
                  {currentPair.extB.toUpperCase()}
                </span>
              </div>
              <span className="text-xs text-[#898781] font-mono">
                {formatTime(currentPair.durB)}
              </span>
            </div>

            <div className="text-sm font-medium text-white truncate mb-1" title={currentPair.relB}>
              {currentPair.relB}
            </div>
            <div className="text-xs text-[#898781] truncate mb-4 font-mono">
              {currentPair.trackB_path}
            </div>

            {/* Waveform B */}
            <div
              onClick={handleWaveformClick}
              className="h-20 bg-[#151515] rounded-xl border border-[#333230] p-2 flex items-center justify-between cursor-pointer relative overflow-hidden group"
            >
              {/* Playhead line */}
              <div
                className="absolute top-0 bottom-0 w-0.5 bg-white z-10 pointer-events-none"
                style={{ left: `${currentPercent}%` }}
              />
              <div
                className="absolute top-0 bottom-0 left-0 bg-[#6da7ec]/10 pointer-events-none"
                style={{ width: `${currentPercent}%` }}
              />

              <div className="flex items-center justify-between w-full h-full space-x-1">
                {waveformB.length > 0
                  ? waveformB.map((val, i) => (
                      <div
                        key={i}
                        className={`flex-1 rounded-full transition-all ${
                          i / waveformB.length <= currentPercent / 100
                            ? "bg-[#6da7ec]"
                            : "bg-[#42403c] group-hover:bg-[#555]"
                        }`}
                        style={{ height: `${Math.max(8, val * 100)}%` }}
                      />
                    ))
                  : Array.from({ length: 60 }).map((_, i) => (
                      <div
                        key={i}
                        className="flex-1 bg-[#333230] rounded-full"
                        style={{ height: `${20 + ((i * 3) % 5) * 15}%` }}
                      />
                    ))}
              </div>
            </div>
          </div>
        </div>

        {/* Global Synchronized Audio Transport Controls */}
        <div className="p-4 bg-[#1c1c1c] rounded-2xl border border-[#333230] flex items-center justify-between">
          <div className="flex items-center space-x-4">
            <button
              onClick={audioToggle}
              className="w-12 h-12 rounded-full bg-[#D97757] hover:bg-[#e58a6d] text-white flex items-center justify-center transition-transform active:scale-95 shadow-md"
            >
              {audioStatus.isPlaying ? (
                <Pause className="w-5 h-5 fill-current" />
              ) : (
                <Play className="w-5 h-5 fill-current ml-0.5" />
              )}
            </button>

            <div className="text-xs font-mono">
              <span className="text-white font-semibold">
                {formatTime(audioStatus.currentTime)}
              </span>
              <span className="text-[#898781]"> / {formatTime(audioStatus.duration)}</span>
            </div>

            <span className="kbd-badge">[Space] Play/Pause</span>
          </div>

          {/* Master Volume Slider */}
          <div className="flex items-center space-x-3 w-48">
            <button
              onClick={() => audioSetVolume(audioStatus.volume > 0 ? 0 : 0.5)}
              className="text-[#898781] hover:text-white"
            >
              {audioStatus.volume === 0 ? (
                <VolumeX className="w-4 h-4" />
              ) : (
                <Volume2 className="w-4 h-4" />
              )}
            </button>
            <input
              type="range"
              min="0"
              max="1"
              step="0.01"
              value={audioStatus.volume}
              onChange={(e) => audioSetVolume(parseFloat(e.target.value))}
              className="w-full accent-[#D97757] h-1.5 bg-[#151515] rounded-lg cursor-pointer"
            />
          </div>
        </div>

        {/* Action Decision Buttons */}
        <div className="grid grid-cols-3 gap-4 pt-2">
          <button
            onClick={() => resolveDuplicate(activePairIndex, "keep_a")}
            className="flex items-center justify-center space-x-2 py-3.5 px-4 rounded-xl bg-[#1c1c1c] border border-emerald-500/40 text-emerald-400 hover:bg-emerald-500/10 font-semibold text-sm transition-all shadow-sm"
          >
            <CheckCircle2 className="w-4 h-4" />
            <span>Оставить A, удалить B</span>
            <span className="kbd-badge text-emerald-400 border-emerald-500/40">[A]</span>
          </button>

          <button
            onClick={() => resolveDuplicate(activePairIndex, "keep_b")}
            className="flex items-center justify-center space-x-2 py-3.5 px-4 rounded-xl bg-[#1c1c1c] border border-[#6da7ec]/40 text-[#6da7ec] hover:bg-[#6da7ec]/10 font-semibold text-sm transition-all shadow-sm"
          >
            <CheckCircle2 className="w-4 h-4" />
            <span>Оставить B, удалить A</span>
            <span className="kbd-badge text-[#6da7ec] border-[#6da7ec]/40">[B]</span>
          </button>

          <button
            onClick={() => resolveDuplicate(activePairIndex, "skip")}
            className="flex items-center justify-center space-x-2 py-3.5 px-4 rounded-xl bg-[#1c1c1c] border border-[#42403c] text-[#c3c2b7] hover:text-white hover:bg-[#252525] font-semibold text-sm transition-all"
          >
            <SkipForward className="w-4 h-4" />
            <span>Пропустить (Оставить оба)</span>
            <span className="kbd-badge">[S]</span>
          </button>
        </div>
      </div>
    </div>
  );
};
