<template>
  <div>
    <div class="toolbar">
      <h2 style="font-size:20px;font-weight:800">收藏夹</h2>
    </div>
    <div v-if="favorites.length === 0" class="empty-state">
      <div class="empty-icon"><i class="fas fa-star"></i></div>
      <h3>暂无收藏</h3>
      <p>点击文件卡片上的星标图标添加收藏</p>
    </div>
    <div v-else class="file-grid">
      <div v-for="file in favorites" :key="file.file_id" class="file-card">
        <div class="card-cover">
          <img v-if="isImage(file)" :src="getImageSrc(file)" loading="lazy" />
          <div v-else class="card-cover-icon" :style="{color: getFileColor(file)}"><i :class="getFileIcon(file)"></i></div>
        </div>
        <div class="card-content">
          <div class="card-title">{{ file.filename }}</div>
          <div class="card-meta">{{ formatSize(file.size) }}</div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import api from '../utils/api'

const favorites = ref([])

onMounted(async () => {
  try {
    const res = await api.get('/files/favorites')
    favorites.value = res.data.data || []
  } catch (e) {}
})

function isImage(f) { return f.mime_type?.startsWith('image/') }
function getFileColor(f) { const ext=f.filename?.split('.').pop()?.toLowerCase();const m={jpg:'#2563eb',pdf:'#ef4444',doc:'#0ea5e9'};return m[ext]||'#64748b' }
function getFileIcon(f) { const m=f.mime_type||'';if(m.includes('pdf'))return'fas fa-file-pdf';if(m.includes('word'))return'fas fa-file-word';return'fas fa-file' }
function getImageSrc(f) { return `/api/i/${f.file_id}?size=400&token=${localStorage.getItem('token')}` }
function formatSize(b) { if(b<1024)return b+'B';if(b<1048576)return(b/1024).toFixed(1)+'KB';if(b<1073741824)return(b/1048576).toFixed(1)+'MB';return(b/1073741824).toFixed(2)+'GB' }
</script>

<style scoped>
.toolbar { margin-bottom: 20px; }
.empty-state { text-align: center; padding: 80px 20px; }
.empty-icon { width: 80px; height: 80px; border-radius: 50%; background: #fef3c7; display: inline-flex; align-items: center; justify-content: center; margin-bottom: 20px; }
.empty-icon i { font-size: 2rem; color: #f59e0b; }
.file-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(340px, 1fr)); gap: 24px; }
.file-card { background: white; border-radius: 30px; overflow: hidden; box-shadow: 0 16px 50px rgba(15,23,42,.05); }
.card-cover { height: 200px; overflow: hidden; background: #f3f4f6; }
.card-cover img { width: 100%; height: 100%; object-fit: cover; }
.card-cover-icon { width: 100%; height: 100%; display: flex; align-items: center; justify-content: center; font-size: 3rem; }
.card-content { padding: 18px; }
.card-title { font-size: 15px; font-weight: 700; }
.card-meta { font-size: 12px; color: #6b7280; margin-top: 4px; }
</style>
