import React, { useEffect, useState } from "react";
import {
  Search,
  Check,
  CheckCheck,
  X,
  Sparkles,
  Image as ImageIcon,
  FileText,
  ChevronDown,
  ChevronUp,
  ExternalLink,
  Edit2,
  RefreshCw,
  FolderSearch,
  Disc,
} from "lucide-react";
import { useAppStore } from "../store";
import { AlbumGroup, TrackItem } from "../types";

export const TagInspector: React.FC = () => {
  const albums = useAppStore((s) => s.albums);
  const selectedAlbumKey = useAppStore((s) => s.selectedAlbumKey);
  const setSelectedAlbumKey = useAppStore((s) => s.setSelectedAlbumKey);
  const filterTier = useAppStore((s) => s.filterTier);
  const setFilterTier = useAppStore((s) => s.setFilterTier);
  const searchQuery = useAppStore((s) => s.searchQuery);
  const setSearchQuery = useAppStore((s) => s.setSearchQuery);
  const tagProgress = useAppStore((s) => s.tagProgress);

  const startTagScan = useAppStore((s) => s.startTagScan);
  const fetchTagProgress = useAppStore((s) => s.fetchTagProgress);
  const fetchAlbums = useAppStore((s) => s.fetchAlbums);
  const approveTrack = useAppStore((s) => s.approveTrack);
  const approveAlbum = useAppStore((s) => s.approveAlbum);
  const skipItem = useAppStore((s) => s.skipItem);
  const selectCandidate = useAppStore((s) => s.selectCandidate);
  const updateTrackDetails = useAppStore((s) => s.updateTrackDetails);
  const setCoverChoice = useAppStore((s) => s.setCoverChoice);
  const manualFetchMetadata = useAppStore((s) => s.manualFetchMetadata);

  const [expandedLyricsTrackIndex, setExpandedLyricsTrackIndex] = useState<number | null>(null);
  const [editingTrackIndex, setEditingTrackIndex] = useState<number | null>(null);
  const [editFields, setEditFields] = useState({ title: "", artist: "", trackNo: "", lyrics: "" });
  const [manualQuery, setManualQuery] = useState("");
  const [manualSource, setManualSource] = useState("mb");

  useEffect(() => {
    fetchAlbums();
    const interval = setInterval(() => {
      fetchTagProgress();
    }, 1000);
    return () => clearInterval(interval);
  }, [fetchAlbums, fetchTagProgress]);

  const selectedAlbum: AlbumGroup | undefined = albums.find(
    (a) => a.albumKey === selectedAlbumKey
  );

  // Filter Albums
  const filteredAlbums = albums.filter((alb) => {
    const q = searchQuery.toLowerCase();
    const matchesSearch =
      alb.album.toLowerCase().includes(q) ||
      alb.artist.toLowerCase().includes(q);

    if (!matchesSearch) return false;

    if (filterTier === "attention") return alb.hasConflict || alb.confidenceScore < 0.8;
    if (filterTier === "tierA") return alb.matchTierName.includes("Tier A") || alb.matchTierName.includes("MusicBrainz");
    if (filterTier === "discogs") return alb.matchTierName.includes("Discogs");
    if (filterTier === "niche") return alb.matchTierName.includes("Niche") || alb.matchTierName.includes("Local");
    return true;
  });

  const handleStartEdit = (t: TrackItem) => {
    setEditingTrackIndex(t.index);
    setEditFields({
      title: t.title,
      artist: t.artist,
      trackNo: t.trackNo,
      lyrics: t.currentLyrics || t.lyricsOriginal || "",
    });
  };

  const handleSaveEdit = (trackIndex: number) => {
    updateTrackDetails(
      trackIndex,
      editFields.title,
      editFields.artist,
      editFields.trackNo,
      editFields.lyrics
    );
    setEditingTrackIndex(null);
  };

  const formatDuration = (sec: number) => {
    if (!sec || sec <= 0) return "--:--";
    const m = Math.floor(sec / 60);
    const s = Math.floor(sec % 60);
    return `${m.toString().padStart(2, "0")}:${s.toString().padStart(2, "0")}`;
  };

  return (
    <div className="flex-1 flex h-full bg-[#151515] overflow-hidden">
      {/* ----------------------------------------------------------------------------------------- */}
      {/* Left Master List: Albums & Releases */}
      {/* ----------------------------------------------------------------------------------------- */}
      <div className="w-80 border-r border-[#333230] flex flex-col h-full bg-[#171717] flex-shrink-0">
        {/* Search & Actions Bar */}
        <div className="p-3 border-b border-[#333230] space-y-2">
          <div className="flex items-center space-x-2">
            <div className="relative flex-1">
              <Search className="w-3.5 h-3.5 text-[#898781] absolute left-2.5 top-2.5" />
              <input
                type="text"
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
                placeholder="Поиск по альбомам/артистам..."
                className="w-full bg-[#1c1c1c] border border-[#333230] rounded-lg pl-8 pr-3 py-1.5 text-xs text-white placeholder-[#898781] focus:outline-none focus:border-[#D97757]"
              />
            </div>
            <button
              onClick={startTagScan}
              disabled={tagProgress.isScanning}
              title="Сканировать папку TO SORT"
              className="p-2 rounded-lg bg-[#D97757] hover:bg-[#e58a6d] text-white disabled:opacity-50 transition-colors"
            >
              <RefreshCw className={`w-3.5 h-3.5 ${tagProgress.isScanning ? "animate-spin" : ""}`} />
            </button>
          </div>

          {/* Progress bar if scanning */}
          {tagProgress.isScanning && (
            <div className="bg-[#1c1c1c] p-2 rounded-lg border border-[#333230] space-y-1">
              <div className="flex justify-between text-[10px] text-[#898781]">
                <span>Сканирование: {tagProgress.done}/{tagProgress.total}</span>
                <span>{(tagProgress.fraction * 100).toFixed(0)}%</span>
              </div>
              <div className="w-full bg-[#151515] h-1.5 rounded-full overflow-hidden">
                <div
                  className="bg-[#D97757] h-full transition-all duration-300"
                  style={{ width: `${tagProgress.fraction * 100}%` }}
                />
              </div>
              <div className="flex justify-between text-[9px] text-[#898781]">
                <span>ETA: {tagProgress.eta}</span>
                <span>{tagProgress.speed.toFixed(1)} тр/сек</span>
              </div>
            </div>
          )}

          {/* Filter Pills */}
          <div className="flex items-center space-x-1 overflow-x-auto pb-1 text-[11px]">
            {[
              { id: "all", label: "Все" },
              { id: "attention", label: "Внимание [!]" },
              { id: "tierA", label: "Tier A" },
              { id: "discogs", label: "Discogs" },
              { id: "niche", label: "Niche" },
            ].map((f) => (
              <button
                key={f.id}
                onClick={() => setFilterTier(f.id)}
                className={`px-2 py-0.5 rounded whitespace-nowrap transition-colors ${
                  filterTier === f.id
                    ? "bg-[#D97757] text-white font-medium"
                    : "bg-[#202020] text-[#898781] hover:text-white"
                }`}
              >
                {f.label}
              </button>
            ))}
          </div>
        </div>

        {/* Album Cards List */}
        <div className="flex-1 overflow-y-auto divide-y divide-[#262626]">
          {filteredAlbums.length === 0 ? (
            <div className="p-6 text-center text-xs text-[#898781]">
              Релизы не найдены. Нажмите кнопку сканирования справа от строки поиска.
            </div>
          ) : (
            filteredAlbums.map((alb) => {
              const isSelected = alb.albumKey === selectedAlbumKey;
              const hasAllProcessed = alb.tracks.every((t) => t.isProcessed);
              return (
                <div
                  key={alb.albumKey}
                  onClick={() => setSelectedAlbumKey(alb.albumKey)}
                  className={`p-3 cursor-pointer flex items-start space-x-3 transition-colors ${
                    isSelected
                      ? "bg-[#222222] border-l-2 border-[#D97757]"
                      : "hover:bg-[#1c1c1c]"
                  } ${hasAllProcessed ? "opacity-50" : ""}`}
                >
                  {/* Thumbnail */}
                  <div className="w-12 h-12 rounded-lg bg-[#1c1c1c] border border-[#333230] overflow-hidden flex-shrink-0 flex items-center justify-center text-[#898781]">
                    {alb.hasOnlineCover || alb.hasLocalCover ? (
                      <img
                        src={`http://127.0.0.1:8765/api/tags/cover?trackIndex=${alb.referenceIndex}&type=${
                          alb.selectedCoverChoice === 1 ? "online" : "local"
                        }`}
                        alt="cover"
                        className="w-full h-full object-cover"
                        onError={(e) => {
                          (e.target as HTMLElement).style.display = "none";
                        }}
                      />
                    ) : (
                      <Disc className="w-6 h-6" />
                    )}
                  </div>

                  {/* Info */}
                  <div className="flex-1 min-w-0">
                    <div className="text-xs font-semibold text-white truncate">
                      {alb.album}
                    </div>
                    <div className="text-[11px] text-[#c3c2b7] truncate">
                      {alb.artist}
                    </div>
                    <div className="flex items-center space-x-2 mt-1">
                      <span className="text-[10px] text-[#898781]">
                        {alb.trackCount} треков
                      </span>
                      <span
                        className={`text-[9px] px-1.5 py-0.2 rounded border ${
                          alb.confidenceScore >= 0.9
                            ? "border-emerald-500/30 text-emerald-400 bg-emerald-500/10"
                            : alb.confidenceScore >= 0.7
                            ? "border-amber-500/30 text-amber-400 bg-amber-500/10"
                            : "border-[#42403c] text-[#898781] bg-[#222]"
                        }`}
                      >
                        {(alb.confidenceScore * 100).toFixed(0)}%
                      </span>
                      {alb.hasConflict && (
                        <span className="text-[9px] px-1 rounded bg-rose-500/20 text-rose-400 border border-rose-500/30 font-bold">
                          !
                        </span>
                      )}
                    </div>
                  </div>
                </div>
              );
            })
          )}
        </div>
      </div>

      {/* ----------------------------------------------------------------------------------------- */}
      {/* Right Details Panel: Inspector */}
      {/* ----------------------------------------------------------------------------------------- */}
      {selectedAlbum ? (
        <div className="flex-1 flex flex-col h-full bg-[#151515] overflow-y-auto">
          {/* Release Header Banner */}
          <div className="p-6 border-b border-[#333230] bg-[#191919] space-y-4">
            <div className="flex items-start justify-between">
              <div>
                <div className="flex items-center space-x-2.5">
                  <h1 className="text-xl font-bold text-white tracking-tight">
                    {selectedAlbum.album}
                  </h1>
                  {selectedAlbum.year && (
                    <span className="text-xs px-2 py-0.5 rounded bg-[#242424] text-[#898781] border border-[#333230]">
                      {selectedAlbum.year}
                    </span>
                  )}
                </div>
                <div className="text-sm text-[#c3c2b7] mt-0.5 font-medium">
                  {selectedAlbum.artist}
                </div>
              </div>

              {/* Match Tier Badge */}
              <div className="flex items-center space-x-2">
                <span className="text-xs px-2.5 py-1 rounded-full bg-[#202020] border border-[#3e3b38] text-[#6da7ec] font-medium">
                  {selectedAlbum.matchTierName}
                </span>
              </div>
            </div>

            {/* Candidate Metadata Selector Dropdown */}
            {selectedAlbum.candidates && selectedAlbum.candidates.length > 0 && (
              <div className="p-3 bg-[#1c1c1c] rounded-xl border border-[#333230] space-y-2">
                <div className="text-xs font-semibold text-[#898781] flex items-center justify-between">
                  <span>Кандидаты метаданных консенсуса:</span>
                  <span className="text-[11px] text-[#D97757]">
                    Уверенность: {(selectedAlbum.confidenceScore * 100).toFixed(0)}%
                  </span>
                </div>
                <div className="flex flex-wrap gap-2">
                  {selectedAlbum.candidates.map((cand, cIdx) => (
                    <button
                      key={cIdx}
                      onClick={() => selectCandidate(selectedAlbum.referenceIndex, cIdx, true)}
                      className="flex items-center space-x-2 px-3 py-1.5 rounded-lg text-xs bg-[#242424] border border-[#333230] hover:border-[#D97757] text-[#c3c2b7] hover:text-white transition-colors"
                    >
                      <span className="font-semibold text-[#6da7ec]">
                        [{cand.source} {(cand.confidenceScore * 100).toFixed(0)}%]
                      </span>
                      <span>{cand.artist} — {cand.title || cand.album}</span>
                    </button>
                  ))}
                </div>
              </div>
            )}

            {/* Manual Query Input */}
            <div className="flex items-center space-x-2 pt-1">
              <select
                value={manualSource}
                onChange={(e) => setManualSource(e.target.value)}
                className="bg-[#1c1c1c] border border-[#333230] text-xs text-[#c3c2b7] rounded-lg px-2.5 py-1.5 focus:outline-none focus:border-[#D97757]"
              >
                <option value="mb">MusicBrainz ID / URL</option>
                <option value="discogs">Discogs Release ID</option>
                <option value="touhoudb">TouhouDB</option>
                <option value="vocadb">VocaDB</option>
                <option value="thwiki">THBWiki</option>
              </select>
              <input
                type="text"
                value={manualQuery}
                onChange={(e) => setManualQuery(e.target.value)}
                placeholder="Вставьте ссылку или ID релиза..."
                className="flex-1 bg-[#1c1c1c] border border-[#333230] text-xs text-white rounded-lg px-3 py-1.5 focus:outline-none focus:border-[#D97757]"
              />
              <button
                onClick={() => {
                  if (manualQuery) {
                    manualFetchMetadata(manualSource, manualQuery, selectedAlbum.referenceIndex, true);
                    setManualQuery("");
                  }
                }}
                className="px-3 py-1.5 bg-[#252525] border border-[#42403c] hover:border-[#D97757] text-xs text-white rounded-lg transition-colors"
              >
                Загрузить
              </button>
            </div>
          </div>

          {/* Cover Art Comparison Block */}
          <div className="p-6 border-b border-[#333230] space-y-3">
            <h3 className="text-xs font-bold uppercase tracking-wider text-[#898781] flex items-center space-x-2">
              <ImageIcon className="w-4 h-4 text-[#D97757]" />
              <span>Сравнение обложек релиза</span>
            </h3>

            <div className="grid grid-cols-2 gap-4">
              {/* Local Cover */}
              <div
                onClick={() => setCoverChoice(selectedAlbum.referenceIndex, 0, true)}
                className={`p-3 rounded-xl bg-[#1c1c1c] border cursor-pointer transition-all ${
                  selectedAlbum.selectedCoverChoice === 0
                    ? "border-[#D97757] shadow-[0_0_15px_rgba(217,119,87,0.2)]"
                    : "border-[#333230] opacity-75 hover:opacity-100"
                }`}
              >
                <div className="flex items-center justify-between mb-2">
                  <span className="text-xs font-semibold text-white">Локальная обложка</span>
                  {selectedAlbum.selectedCoverChoice === 0 && (
                    <span className="text-[10px] px-1.5 py-0.5 rounded bg-[#D97757] text-white font-bold">
                      ВЫБРАНА
                    </span>
                  )}
                </div>
                <div className="w-full h-44 rounded-lg bg-[#151515] border border-[#333230] overflow-hidden flex items-center justify-center text-[#898781]">
                  {selectedAlbum.hasLocalCover ? (
                    <img
                      src={`http://127.0.0.1:8765/api/tags/cover?trackIndex=${selectedAlbum.referenceIndex}&type=local`}
                      alt="Local Cover"
                      className="w-full h-full object-contain"
                    />
                  ) : (
                    <div className="text-xs text-[#898781]">Нет локального файла обложки</div>
                  )}
                </div>
              </div>

              {/* Online Cover */}
              <div
                onClick={() => setCoverChoice(selectedAlbum.referenceIndex, 1, true)}
                className={`p-3 rounded-xl bg-[#1c1c1c] border cursor-pointer transition-all ${
                  selectedAlbum.selectedCoverChoice === 1
                    ? "border-[#6da7ec] shadow-[0_0_15px_rgba(109,167,236,0.2)]"
                    : "border-[#333230] opacity-75 hover:opacity-100"
                }`}
              >
                <div className="flex items-center justify-between mb-2">
                  <span className="text-xs font-semibold text-white">
                    Онлайн-скан {selectedAlbum.onlineCoverSource && `(${selectedAlbum.onlineCoverSource})`}
                  </span>
                  {selectedAlbum.selectedCoverChoice === 1 && (
                    <span className="text-[10px] px-1.5 py-0.5 rounded bg-[#6da7ec] text-[#151515] font-bold">
                      ВЫБРАНА
                    </span>
                  )}
                </div>
                <div className="w-full h-44 rounded-lg bg-[#151515] border border-[#333230] overflow-hidden flex items-center justify-center text-[#898781]">
                  {selectedAlbum.hasOnlineCover ? (
                    <img
                      src={`http://127.0.0.1:8765/api/tags/cover?trackIndex=${selectedAlbum.referenceIndex}&type=online`}
                      alt="Online Cover"
                      className="w-full h-full object-contain"
                    />
                  ) : (
                    <div className="text-xs text-[#898781]">Онлайн обложка не найдена</div>
                  )}
                </div>
              </div>
            </div>
          </div>

          {/* Interactive Tracklist */}
          <div className="p-6 flex-1 space-y-3">
            <h3 className="text-xs font-bold uppercase tracking-wider text-[#898781] flex items-center justify-between">
              <span>Треклист ({selectedAlbum.tracks.length} треков)</span>
              <span className="text-[11px] font-normal text-[#898781]">
                Кликните на трек для инлайн-редактирования
              </span>
            </h3>

            <div className="bg-[#1c1c1c] rounded-xl border border-[#333230] overflow-hidden">
              <table className="w-full text-left text-xs border-collapse">
                <thead>
                  <tr className="border-b border-[#333230] bg-[#202020] text-[#898781]">
                    <th className="py-2.5 px-3 w-10 text-center">№</th>
                    <th className="py-2.5 px-3">Название трека</th>
                    <th className="py-2.5 px-3">Исполнитель</th>
                    <th className="py-2.5 px-3 w-20 text-right">Длина</th>
                    <th className="py-2.5 px-3 w-16 text-center">Текст</th>
                    <th className="py-2.5 px-3 w-28 text-right">Действия</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-[#262626]">
                  {selectedAlbum.tracks.map((t) => {
                    const isEditing = editingTrackIndex === t.index;
                    const isLyricsExpanded = expandedLyricsTrackIndex === t.index;
                    return (
                      <React.Fragment key={t.index}>
                        <tr
                          className={`hover:bg-[#232323] transition-colors ${
                            t.isProcessed ? "opacity-40" : ""
                          }`}
                        >
                          <td className="py-2.5 px-3 text-center font-mono text-[#898781]">
                            {isEditing ? (
                              <input
                                type="text"
                                value={editFields.trackNo}
                                onChange={(e) =>
                                  setEditFields({ ...editFields, trackNo: e.target.value })
                                }
                                className="w-8 bg-[#151515] border border-[#42403c] rounded text-center text-white"
                              />
                            ) : (
                              t.trackNo
                            )}
                          </td>

                          <td className="py-2.5 px-3 font-medium text-white">
                            {isEditing ? (
                              <input
                                type="text"
                                value={editFields.title}
                                onChange={(e) =>
                                  setEditFields({ ...editFields, title: e.target.value })
                                }
                                className="w-full bg-[#151515] border border-[#42403c] rounded px-2 py-0.5 text-white"
                              />
                            ) : (
                              <div className="flex items-center space-x-2">
                                <span>{t.title}</span>
                                {t.lyricsRomaji && (
                                  <span className="text-[10px] text-[#6da7ec] font-normal">
                                    [Romaji]
                                  </span>
                                )}
                              </div>
                            )}
                          </td>

                          <td className="py-2.5 px-3 text-[#c3c2b7]">
                            {isEditing ? (
                              <input
                                type="text"
                                value={editFields.artist}
                                onChange={(e) =>
                                  setEditFields({ ...editFields, artist: e.target.value })
                                }
                                className="w-full bg-[#151515] border border-[#42403c] rounded px-2 py-0.5 text-white"
                              />
                            ) : (
                              t.artist
                            )}
                          </td>

                          <td className="py-2.5 px-3 text-right font-mono text-[#898781]">
                            {formatDuration(t.duration)}
                          </td>

                          <td className="py-2.5 px-3 text-center">
                            {(t.hasLyrics || t.hasSyncedLyrics || t.lyricsOriginal) ? (
                              <button
                                onClick={() =>
                                  setExpandedLyricsTrackIndex(
                                    isLyricsExpanded ? null : t.index
                                  )
                                }
                                className={`p-1 rounded transition-colors ${
                                  t.hasSyncedLyrics
                                    ? "text-[#6da7ec] hover:bg-[#6da7ec]/10"
                                    : "text-[#c3c2b7] hover:bg-[#333]"
                                }`}
                                title="Посмотреть текст (LRC)"
                              >
                                <FileText className="w-3.5 h-3.5" />
                              </button>
                            ) : (
                              <span className="text-[#555]">—</span>
                            )}
                          </td>

                          <td className="py-2.5 px-3 text-right space-x-1.5">
                            {isEditing ? (
                              <button
                                onClick={() => handleSaveEdit(t.index)}
                                className="px-2 py-1 bg-emerald-500/20 text-emerald-400 border border-emerald-500/30 rounded text-[11px] font-medium"
                              >
                                Сохранить
                              </button>
                            ) : (
                              <>
                                <button
                                  onClick={() => handleStartEdit(t)}
                                  className="p-1 text-[#898781] hover:text-white rounded"
                                  title="Редактировать"
                                >
                                  <Edit2 className="w-3.5 h-3.5" />
                                </button>
                                <button
                                  onClick={() => approveTrack(t.index)}
                                  className="p-1 text-[#D97757] hover:text-[#e58a6d] rounded font-bold"
                                  title="Принять трек [Enter]"
                                >
                                  <Check className="w-3.5 h-3.5" />
                                </button>
                              </>
                            )}
                          </td>
                        </tr>

                        {/* Expandable Synchronized Lyrics Drawer */}
                        {isLyricsExpanded && (
                          <tr className="bg-[#171717]">
                            <td colSpan={6} className="p-4 border-t border-[#2a2a2a]">
                              <div className="space-y-2">
                                <div className="flex items-center justify-between">
                                  <span className="text-xs font-semibold text-white flex items-center space-x-2">
                                    <FileText className="w-3.5 h-3.5 text-[#6da7ec]" />
                                    <span>Текст песни: {t.title}</span>
                                    {t.hasSyncedLyrics && (
                                      <span className="text-[10px] px-1.5 rounded bg-[#6da7ec]/20 text-[#6da7ec] border border-[#6da7ec]/30">
                                        Синхронизированный (LRC)
                                      </span>
                                    )}
                                  </span>
                                  <button
                                    onClick={() => setExpandedLyricsTrackIndex(null)}
                                    className="text-xs text-[#898781] hover:text-white"
                                  >
                                    Закрыть
                                  </button>
                                </div>
                                <pre className="text-xs font-mono text-[#c3c2b7] bg-[#121212] p-3 rounded-lg max-h-48 overflow-y-auto whitespace-pre-wrap leading-relaxed border border-[#2a2a2a]">
                                  {t.currentLyrics || t.lyricsRomaji || t.lyricsOriginal || "Текст песни отсутствует."}
                                </pre>
                              </div>
                            </td>
                          </tr>
                        )}
                      </React.Fragment>
                    );
                  })}
                </tbody>
              </table>
            </div>
          </div>

          {/* Bottom Fast Action Buttons Bar */}
          <div className="sticky bottom-0 p-4 bg-[#191919] border-t border-[#333230] flex items-center justify-between shadow-2xl">
            <div className="flex items-center space-x-2 text-xs text-[#898781]">
              <span>Горячие клавиши:</span>
              <span className="kbd-badge">[Enter] Трек</span>
              <span className="kbd-badge">[Ctrl+Enter] Альбом</span>
              <span className="kbd-badge">[Esc] Пропустить</span>
            </div>

            <div className="flex items-center space-x-3">
              <button
                onClick={() => skipItem(selectedAlbum.referenceIndex, true)}
                className="px-4 py-2 rounded-xl bg-[#222] border border-[#333230] text-xs text-[#c3c2b7] hover:text-white transition-colors"
              >
                Пропустить альбом
              </button>

              <button
                onClick={() => {
                  const firstUnproc = selectedAlbum.tracks.find((t) => !t.isProcessed);
                  if (firstUnproc) approveTrack(firstUnproc.index);
                }}
                className="flex items-center space-x-2 px-4 py-2 rounded-xl bg-[#242424] border border-[#D97757]/40 text-[#D97757] hover:bg-[#D97757]/10 text-xs font-semibold transition-colors"
              >
                <Check className="w-3.5 h-3.5" />
                <span>Принять трек</span>
                <span className="kbd-badge text-[#D97757] border-[#D97757]/40">[Enter]</span>
              </button>

              <button
                onClick={() => approveAlbum(selectedAlbum.referenceIndex)}
                className="flex items-center space-x-2 px-5 py-2 rounded-xl bg-[#D97757] hover:bg-[#e58a6d] text-white text-xs font-semibold transition-all shadow-lg"
              >
                <CheckCheck className="w-4 h-4" />
                <span>Принять весь альбом</span>
                <span className="kbd-badge text-white bg-[#a85237] border-[#bf6043]">[Ctrl+Enter]</span>
              </button>
            </div>
          </div>
        </div>
      ) : (
        <div className="flex-1 flex items-center justify-center text-xs text-[#898781]">
          Выберите альбом слева для инспекции тегов и обложек
        </div>
      )}
    </div>
  );
};
