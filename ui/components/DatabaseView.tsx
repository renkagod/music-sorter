import React, { useEffect } from "react";
import { Database, Search, RefreshCw, FileDown, CheckCircle, AlertCircle, Music } from "lucide-react";
import { useAppStore } from "../store";

export const DatabaseView: React.FC = () => {
  const databaseTracks = useAppStore((s) => s.databaseTracks);
  const databaseStats = useAppStore((s) => s.databaseStats);
  const dbFilterStatus = useAppStore((s) => s.dbFilterStatus);
  const dbFilterFormat = useAppStore((s) => s.dbFilterFormat);
  const dbSearchQuery = useAppStore((s) => s.dbSearchQuery);
  const setDbFilterStatus = useAppStore((s) => s.setDbFilterStatus);
  const setDbFilterFormat = useAppStore((s) => s.setDbFilterFormat);
  const setDbSearchQuery = useAppStore((s) => s.setDbSearchQuery);
  const queryDatabase = useAppStore((s) => s.queryDatabase);
  const syncDatabase = useAppStore((s) => s.syncDatabase);
  const exportDatabase = useAppStore((s) => s.exportDatabase);

  useEffect(() => {
    queryDatabase();
  }, [queryDatabase, dbFilterStatus, dbFilterFormat, dbSearchQuery]);

  return (
    <div className="flex-1 flex flex-col h-full bg-[#151515] p-8 overflow-hidden space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-xl font-bold text-white flex items-center space-x-2">
            <Database className="w-5 h-5 text-[#D97757]" />
            <span>База треков</span>
          </h1>
          <p className="text-xs text-[#898781] mt-0.5">
            Список треков из tracklist.md с отметками о наличии файлов на диске.
          </p>
        </div>

        <div className="flex items-center space-x-3">
          <button
            onClick={() => syncDatabase()}
            className="flex items-center space-x-2 px-4 py-2 rounded-xl bg-[#222] border border-[#333230] hover:border-[#D97757] text-xs text-white font-medium transition-colors"
          >
            <RefreshCw className="w-3.5 h-3.5" />
            <span>Синхронизировать с диском</span>
          </button>

          <button
            onClick={() => exportDatabase()}
            className="flex items-center space-x-2 px-4 py-2 rounded-xl bg-[#D97757] hover:bg-[#e58a6d] text-xs text-white font-semibold transition-all shadow-md"
          >
            <FileDown className="w-3.5 h-3.5" />
            <span>Экспорт в Markdown</span>
          </button>
        </div>
      </div>

      {/* Stats Summary Cards */}
      <div className="grid grid-cols-3 gap-4">
        <div className="p-4 bg-[#1c1c1c] rounded-2xl border border-[#333230] flex items-center justify-between">
          <div>
            <div className="text-xl font-bold text-white">{databaseStats.totalTracks}</div>
            <div className="text-xs text-[#898781]">Всего треков в базе</div>
          </div>
          <div className="w-10 h-10 rounded-xl bg-[#252525] flex items-center justify-center text-[#D97757]">
            <Music className="w-5 h-5" />
          </div>
        </div>

        <div className="p-4 bg-[#1c1c1c] rounded-2xl border border-[#333230] flex items-center justify-between">
          <div>
            <div className="text-xl font-bold text-emerald-400">{databaseStats.downloadedTracks}</div>
            <div className="text-xs text-[#898781]">В наличии на диске</div>
          </div>
          <div className="w-10 h-10 rounded-xl bg-emerald-500/10 flex items-center justify-center text-emerald-400">
            <CheckCircle className="w-5 h-5" />
          </div>
        </div>

        <div className="p-4 bg-[#1c1c1c] rounded-2xl border border-[#333230] flex items-center justify-between">
          <div>
            <div className="text-xl font-bold text-amber-400">{databaseStats.missingTracks}</div>
            <div className="text-xs text-[#898781]">Нет на диске</div>
          </div>
          <div className="w-10 h-10 rounded-xl bg-amber-500/10 flex items-center justify-center text-amber-400">
            <AlertCircle className="w-5 h-5" />
          </div>
        </div>
      </div>

      {/* Filter and Search Bar */}
      <div className="flex items-center space-x-3 bg-[#191919] p-3 rounded-xl border border-[#333230]">
        <div className="relative flex-1">
          <Search className="w-3.5 h-3.5 text-[#898781] absolute left-3 top-2.5" />
          <input
            type="text"
            value={dbSearchQuery}
            onChange={(e) => setDbSearchQuery(e.target.value)}
            placeholder="Поиск по исполнителю, названию или альбому..."
            className="w-full bg-[#151515] border border-[#333230] rounded-lg pl-9 pr-3 py-1.5 text-xs text-white placeholder-[#898781] focus:outline-none focus:border-[#D97757]"
          />
        </div>

        <div className="flex items-center space-x-2 text-xs">
          <select
            value={dbFilterStatus}
            onChange={(e) => setDbFilterStatus(parseInt(e.target.value))}
            className="bg-[#151515] border border-[#333230] rounded-lg px-2.5 py-1.5 text-[#c3c2b7] focus:outline-none focus:border-[#D97757]"
          >
            <option value="-1">Все статусы</option>
            <option value="1">В наличии [x]</option>
            <option value="0">Нет на диске [ ]</option>
          </select>

          <select
            value={dbFilterFormat}
            onChange={(e) => setDbFilterFormat(parseInt(e.target.value))}
            className="bg-[#151515] border border-[#333230] rounded-lg px-2.5 py-1.5 text-[#c3c2b7] focus:outline-none focus:border-[#D97757]"
          >
            <option value="-1">Все форматы</option>
            <option value="1">FLAC</option>
            <option value="2">MP3</option>
          </select>
        </div>
      </div>

      {/* Table */}
      <div className="flex-1 bg-[#1c1c1c] rounded-xl border border-[#333230] overflow-y-auto">
        <table className="w-full text-left text-xs border-collapse">
          <thead className="sticky top-0 bg-[#202020] border-b border-[#333230] text-[#898781] z-10">
            <tr>
              <th className="py-2.5 px-4 w-12 text-center">Статус</th>
              <th className="py-2.5 px-4">Исполнитель</th>
              <th className="py-2.5 px-4">Альбом</th>
              <th className="py-2.5 px-4">Название</th>
              <th className="py-2.5 px-4 w-20 text-center">Формат</th>
              <th className="py-2.5 px-4 w-20 text-right">Битрейт</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-[#262626]">
            {databaseTracks.length === 0 ? (
              <tr>
                <td colSpan={6} className="py-8 text-center text-[#898781]">
                  Треки не найдены
                </td>
              </tr>
            ) : (
              databaseTracks.map((tr) => (
                <tr key={tr.id} className="hover:bg-[#222] transition-colors">
                  <td className="py-2 px-4 text-center">
                    {tr.status === 1 ? (
                      <span className="text-emerald-400 font-bold">[x]</span>
                    ) : (
                      <span className="text-amber-500 font-bold">[ ]</span>
                    )}
                  </td>
                  <td className="py-2 px-4 font-medium text-white">{tr.artist}</td>
                  <td className="py-2 px-4 text-[#c3c2b7]">{tr.album}</td>
                  <td className="py-2 px-4 text-white">{tr.title}</td>
                  <td className="py-2 px-4 text-center">
                    <span
                      className={`px-1.5 py-0.5 rounded text-[10px] font-mono border ${
                        tr.format === "FLAC"
                          ? "border-[#D97757]/30 text-[#D97757] bg-[#D97757]/10"
                          : tr.format === "MP3"
                          ? "border-[#6da7ec]/30 text-[#6da7ec] bg-[#6da7ec]/10"
                          : "border-[#42403c] text-[#898781]"
                      }`}
                    >
                      {tr.format}
                    </span>
                  </td>
                  <td className="py-2 px-4 text-right font-mono text-[#898781]">
                    {tr.bitrateKbps > 0 ? `${tr.bitrateKbps}k` : "-"}
                  </td>
                </tr>
              ))
            )}
          </tbody>
        </table>
      </div>
    </div>
  );
};
