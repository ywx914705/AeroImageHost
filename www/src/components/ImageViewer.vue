<template>
  <Teleport to="body">
    <div v-if="visible" class="viewer-overlay" @click.self="$emit('close')">
      <button class="viewer-close" @click="$emit('close')"><i class="fas fa-times"></i></button>
      <div class="viewer-content" @wheel.prevent="handleWheel">
        <img :src="currentSrc" :style="{ transform: `scale(${zoom}) translate(${panX}px, ${panY}px)` }" @mousedown="startDrag" draggable="false" />
      </div>
      <div class="viewer-toolbar">
        <button @click="zoom = Math.max(0.1, zoom - 0.2)"><i class="fas fa-minus"></i></button>
        <span>{{ Math.round(zoom * 100) }}%</span>
        <button @click="zoom = Math.min(10, zoom + 0.2)"><i class="fas fa-plus"></i></button>
        <button @click="zoom = 1; panX = 0; panY = 0"><i class="fas fa-expand"></i></button>
        <div style="width:1px;height:24px;background:#e5e7eb;margin:0 8px"></div>
        <button @click="$emit('prev')" v-if="hasPrev"><i class="fas fa-chevron-left"></i></button>
        <button @click="$emit('next')" v-if="hasNext"><i class="fas fa-chevron-right"></i></button>
      </div>
      <div class="viewer-info" v-if="file">
        <div class="info-row"><span class="info-label">文件名</span><span>{{ file.filename }}</span></div>
        <div class="info-row"><span class="info-label">大小</span><span>{{ formatSize(file.size) }}</span></div>
        <div class="info-row"><span class="info-label">类型</span><span>{{ file.mime_type }}</span></div>
      </div>
    </div>
  </Teleport>
</template>

<script setup>
import { ref, watch } from 'vue'

const props = defineProps({
  visible: Boolean,
  file: Object,
  files: { type: Array, default: () => [] }
})

const emit = defineEmits(['close', 'prev', 'next'])

const zoom = ref(1)
const panX = ref(0)
const panY = ref(0)
const isDragging = ref(false)
const startX = ref(0)
const startY = ref(0)

const currentSrc = ref('')
const hasPrev = ref(false)
const hasNext = ref(false)

watch(() => props.file, (f) => {
  if (f) {
    currentSrc.value = `/api/i/${f.file_id}?token=${localStorage.getItem('token') || ''}`
    const idx = props.files.findIndex(x => x.file_id === f.file_id)
    hasPrev.value = idx > 0
    hasNext.value = idx < props.files.length - 1
    zoom.value = 1; panX.value = 0; panY.value = 0
  }
}, { immediate: true })

function handleWheel(e) {
  zoom.value = Math.max(0.1, Math.min(10, zoom.value + (e.deltaY > 0 ? -0.1 : 0.1)))
}

function startDrag(e) {
  isDragging.value = true
  startX.value = e.clientX - panX.value
  startY.value = e.clientY - panY.value
  const onMove = (ev) => { if (isDragging.value) { panX.value = ev.clientX - startX.value; panY.value = ev.clientY - startY.value } }
  const onUp = () => { isDragging.value = false; document.removeEventListener('mousemove', onMove); document.removeEventListener('mouseup', onUp) }
  document.addEventListener('mousemove', onMove)
  document.addEventListener('mouseup', onUp)
}

function formatSize(b) { if(b<1024)return b+'B';if(b<1048576)return(b/1024).toFixed(1)+'KB';if(b<1073741824)return(b/1048576).toFixed(1)+'MB';return(b/1073741824).toFixed(2)+'GB' }
</script>

<style scoped>
.viewer-overlay { position: fixed; inset: 0; background: rgba(0,0,0,.88); backdrop-filter: blur(8px); display: flex; align-items: center; justify-content: center; z-index: 9999; }
.viewer-close { position: absolute; top: 24px; right: 24px; width: 48px; height: 48px; border-radius: 14px; border: none; background: rgba(255,255,255,.12); color: white; cursor: pointer; font-size: 18px; display: flex; align-items: center; justify-content: center; }
.viewer-close:hover { background: rgba(255,255,255,.2); }
.viewer-content { max-width: 85vw; max-height: 85vh; cursor: grab; }
.viewer-content img { max-width: 85vw; max-height: 85vh; object-fit: contain; border-radius: 8px; transition: transform .1s; user-select: none; }
.viewer-toolbar { position: absolute; bottom: 24px; left: 50%; transform: translateX(-50%); display: flex; align-items: center; gap: 8px; background: rgba(255,255,255,.12); backdrop-filter: blur(12px); padding: 8px 16px; border-radius: 14px; color: white; }
.viewer-toolbar button { width: 36px; height: 36px; border-radius: 10px; border: none; background: transparent; color: white; cursor: pointer; display: flex; align-items: center; justify-content: center; }
.viewer-toolbar button:hover { background: rgba(255,255,255,.15); }
.viewer-toolbar span { font-size: 13px; font-weight: 600; min-width: 50px; text-align: center; }
.viewer-info { position: absolute; top: 24px; left: 24px; background: rgba(255,255,255,.12); backdrop-filter: blur(12px); padding: 16px 20px; border-radius: 14px; color: white; min-width: 200px; }
.info-row { display: flex; justify-content: space-between; gap: 20px; font-size: 13px; padding: 4px 0; }
.info-label { color: rgba(255,255,255,.6); }
</style>
