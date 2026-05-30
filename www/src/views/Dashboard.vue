<template>
  <div>
    <div class="stats">
      <div class="stat" v-for="s in statCards" :key="s.label">
        <div class="stat-label">{{ s.label }}</div>
        <div class="stat-value">{{ s.value }}</div>
        <div class="stat-desc">{{ s.desc }}</div>
      </div>
    </div>
    <div class="timeline">
      <div class="time-card" v-for="t in timeCards" :key="t.title">
        <h3>{{ t.title }}</h3>
        <p>{{ t.desc }}</p>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted } from 'vue'
import { useAppStore } from '../stores/app'
import { useFilesStore } from '../stores/files'

const appStore = useAppStore()
const filesStore = useFilesStore()

onMounted(() => {
  appStore.loadStats()
  filesStore.loadFiles()
})

const statCards = computed(() => [
  { label: '文件总数', value: appStore.stats.total_files || 0, desc: '支持 26 种格式' },
  { label: '图片数量', value: appStore.stats.total_images || 0, desc: '高质量缩略图' },
  { label: '存储空间', value: formatSize(appStore.stats.total_size || 0), desc: '安全加密存储' },
  { label: '分享次数', value: '-', desc: 'CDN 全球加速' },
])

const timeCards = [
  { title: '最近上传', desc: '查看最近上传的文件' },
  { title: '快速操作', desc: '拖拽文件到上传中心\n支持批量上传\n自动生成外链' },
]

function formatSize(bytes) {
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB'
  if (bytes < 1073741824) return (bytes / 1048576).toFixed(1) + ' MB'
  return (bytes / 1073741824).toFixed(2) + ' GB'
}
</script>

<style scoped>
.stats { display: grid; grid-template-columns: repeat(4, 1fr); gap: 22px; }
.stat { background: white; padding: 24px; border-radius: 28px; box-shadow: 0 14px 50px rgba(15,23,42,.05); }
.stat-label { font-size: 13px; color: #6b7280; }
.stat-value { margin-top: 18px; font-size: 36px; font-weight: 800; }
.stat-desc { margin-top: 12px; font-size: 12px; font-weight: 700; color: #10b981; }
.timeline { margin-top: 28px; display: flex; gap: 18px; overflow-x: auto; }
.time-card { min-width: 240px; background: white; padding: 22px; border-radius: 24px; box-shadow: 0 12px 40px rgba(15,23,42,.04); }
.time-card h3 { font-size: 16px; font-weight: 800; }
.time-card p { margin-top: 12px; line-height: 1.8; font-size: 13px; color: #6b7280; white-space: pre-line; }
@media(max-width: 1200px) { .stats { grid-template-columns: repeat(2, 1fr); } }
@media(max-width: 768px) { .stats { grid-template-columns: 1fr; } }
</style>
