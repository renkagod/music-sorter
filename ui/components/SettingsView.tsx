import React, { useEffect, useState } from "react";
import { Settings as SettingsIcon, Save, Folder, Key, Check } from "lucide-react";
import { useAppStore } from "../store";

export const SettingsView: React.FC = () => {
  const settings = useAppStore((s) => s.settings);
  const fetchSettings = useAppStore((s) => s.fetchSettings);
  const saveSettings = useAppStore((s) => s.saveSettings);

  const [form, setForm] = useState(settings);
  const [saved, setSaved] = useState(false);

  useEffect(() => {
    fetchSettings();
  }, [fetchSettings]);

  useEffect(() => {
    setForm(settings);
  }, [settings]);

  const handleSave = async () => {
    await saveSettings(form);
    setSaved(true);
    setTimeout(() => setSaved(false), 2000);
  };

  return (
    <div className="flex-1 flex flex-col h-full bg-[#151515] p-8 overflow-y-auto max-w-4xl space-y-8">
      <div>
        <h1 className="text-xl font-bold text-white flex items-center space-x-2">
          <SettingsIcon className="w-5 h-5 text-[#D97757]" />
          <span>Настройки</span>
        </h1>
        <p className="text-xs text-[#898781] mt-0.5">
          Пути к рабочим папкам и ключи внешних сервисов. Сохраняются в файле folders.cfg.
        </p>
      </div>

      {/* Folders Section */}
      <div className="p-6 bg-[#1c1c1c] rounded-2xl border border-[#333230] space-y-4">
        <h2 className="text-sm font-semibold text-white flex items-center space-x-2">
          <Folder className="w-4 h-4 text-[#D97757]" />
          <span>Рабочие папки</span>
        </h2>

        <div className="space-y-3">
          <div>
            <label className="block text-xs font-medium text-[#c3c2b7] mb-1">
              Папка для разбора
            </label>
            <input
              type="text"
              value={form.toSortDir}
              onChange={(e) => setForm({ ...form, toSortDir: e.target.value })}
              className="w-full bg-[#151515] border border-[#333230] rounded-lg px-3 py-2 text-xs text-white focus:outline-none focus:border-[#D97757]"
            />
          </div>

          <div>
            <label className="block text-xs font-medium text-[#c3c2b7] mb-1">
              Основная выходная папка
            </label>
            <input
              type="text"
              value={form.outputDir}
              onChange={(e) => setForm({ ...form, outputDir: e.target.value })}
              className="w-full bg-[#151515] border border-[#333230] rounded-lg px-3 py-2 text-xs text-white focus:outline-none focus:border-[#D97757]"
            />
          </div>

          <div className="grid grid-cols-2 gap-4">
            <div>
              <label className="block text-xs font-medium text-[#c3c2b7] mb-1">
                Папка для FLAC
              </label>
              <input
                type="text"
                value={form.flacDir}
                onChange={(e) => setForm({ ...form, flacDir: e.target.value })}
                className="w-full bg-[#151515] border border-[#333230] rounded-lg px-3 py-2 text-xs text-white focus:outline-none focus:border-[#D97757]"
              />
            </div>

            <div>
              <label className="block text-xs font-medium text-[#c3c2b7] mb-1">
                Папка для MP3
              </label>
              <input
                type="text"
                value={form.mp3Dir}
                onChange={(e) => setForm({ ...form, mp3Dir: e.target.value })}
                className="w-full bg-[#151515] border border-[#333230] rounded-lg px-3 py-2 text-xs text-white focus:outline-none focus:border-[#D97757]"
              />
            </div>
          </div>
        </div>
      </div>

      {/* API Keys Section */}
      <div className="p-6 bg-[#1c1c1c] rounded-2xl border border-[#333230] space-y-4">
        <h2 className="text-sm font-semibold text-white flex items-center space-x-2">
          <Key className="w-4 h-4 text-[#6da7ec]" />
          <span>Ключи сервисов</span>
        </h2>

        <div className="space-y-3">
          <div>
            <label className="block text-xs font-medium text-[#c3c2b7] mb-1">
              Ключ AcoustID
            </label>
            <input
              type="text"
              value={form.acoustIdKey}
              onChange={(e) => setForm({ ...form, acoustIdKey: e.target.value })}
              placeholder="kMX2Acy8kr..."
              className="w-full bg-[#151515] border border-[#333230] rounded-lg px-3 py-2 text-xs text-white focus:outline-none focus:border-[#D97757]"
            />
            <p className="text-[11px] text-[#898781] mt-1">
              Ключ приложения с acoustid.org для распознавания треков по аудиоотпечаткам.
            </p>
          </div>

          <div>
            <label className="block text-xs font-medium text-[#c3c2b7] mb-1">
              Токен Discogs
            </label>
            <input
              type="text"
              value={form.discogsToken}
              onChange={(e) => setForm({ ...form, discogsToken: e.target.value })}
              placeholder="Токен Discogs..."
              className="w-full bg-[#151515] border border-[#333230] rounded-lg px-3 py-2 text-xs text-white focus:outline-none focus:border-[#D97757]"
            />
            <p className="text-[11px] text-[#898781] mt-1">
              Персональный токен из настроек профиля Discogs для поиска релизов.
            </p>
          </div>
        </div>
      </div>

      {/* Save Button */}
      <div className="flex items-center space-x-3">
        <button
          onClick={handleSave}
          className="flex items-center space-x-2 px-6 py-2.5 rounded-xl bg-[#D97757] hover:bg-[#e58a6d] text-white text-xs font-semibold transition-all shadow-lg"
        >
          {saved ? <Check className="w-4 h-4" /> : <Save className="w-4 h-4" />}
          <span>{saved ? "Сохранено" : "Сохранить настройки"}</span>
        </button>
      </div>
    </div>
  );
};
