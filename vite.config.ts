import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { viteStaticCopy } from 'vite-plugin-static-copy'

// https://vite.dev/config/
export default defineConfig({
  base: '/dx-api-explorer/',
  plugins: [
    react(),
    viteStaticCopy({
      targets: [
        {
          src: './node_modules/@pega/auth/lib/oauth-client/*Done.*',
          dest: './'
        }
      ]
    })
  ],
  build: {
    outDir: './docs'
  },
  server: {
    proxy: {
      '/prweb': {
        target: 'http://localhost:3333',
        changeOrigin: true
      },
    }
  }
})
