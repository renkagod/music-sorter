/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./ui/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        claude: {
          bg: "#151515",
          surface: "#1c1c1c",
          surfaceHover: "#262626",
          surfaceActive: "#2e2e2e",
          border: "#333230",
          borderMuted: "#42403c",
          muted: "#898781",
          text: "#c3c2b7",
          textDim: "#a19f97",
          accent: "#D97757",
          accentHover: "#e58a6d",
          blue: "#6da7ec",
          blueHover: "#84b7f5",
          white: "#ffffff",
          success: "#4ade80",
          warning: "#facc15",
          danger: "#f87171",
        }
      },
      fontFamily: {
        sans: [
          "-apple-system",
          "BlinkMacSystemFont",
          "'Segoe UI'",
          "Roboto",
          "Oxygen",
          "Ubuntu",
          "Cantarell",
          "sans-serif"
        ],
        mono: [
          "ui-monospace",
          "SFMono-Regular",
          "Menlo",
          "Monaco",
          "Consolas",
          "'Liberation Mono'",
          "monospace"
        ]
      }
    },
  },
  plugins: [],
}
