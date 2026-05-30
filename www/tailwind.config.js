/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{vue,js}'],
  theme: {
    extend: {
      colors: {
        primary: '#2563eb',
        surface: '#ffffff',
        muted: '#6b7280',
        border: '#e5e7eb'
      },
      borderRadius: {
        'xl': '16px',
        '2xl': '24px',
        '3xl': '32px'
      }
    }
  },
  plugins: []
}
