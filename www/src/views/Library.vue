<template>
  <div>
    <div class="toolbar">
      <div class="toolbar-left">
        <button v-for="tab in tabs" :key="tab.key" class="chip" :class="{ active: filesStore.typeFilter === tab.key }" @click="filesStore.typeFilter = tab.key; filesStore.page = 1; filesStore.loadFiles()">
          <i :class="tab.icon"></i> {{ tab.label }}
          <span class="count">{{ tab.count }}</span>
        </button>
        <button class="chip chip-ai" @click="applySuggestion('tag:ai')">
          <i class="fas fa-robot"></i> AI 标签
          <span class="chip-badge">Beta</span>
        </button>
      </div>
      <div class="toolbar-right">
        <div class="search-wrapper">
          <div class="lib-search-box">
            <i class="fas fa-search" style="color:#9ca3af"></i>
            <input
              v-model="searchQuery"
              placeholder="搜索文件... (type:xxx, is:public)"
              @keyup.enter="handleSearch"
              @focus="showSuggestions = !searchQuery"
              @blur="hideSuggestions"
            />
            <button v-if="searchQuery" class="search-clear" @click="clearSearch"><i class="fas fa-times"></i></button>
          </div>
          <div class="search-suggestions" v-if="showSuggestions">
            <div class="suggestion-item" @mousedown.prevent="applySuggestion('type:image')">
              <i class="fas fa-image"></i> 搜索图片
            </div>
            <div class="suggestion-item" @mousedown.prevent="applySuggestion('type:document')">
              <i class="fas fa-file-alt"></i> 搜索文档
            </div>
            <div class="suggestion-item" @mousedown.prevent="applySuggestion('is:public')">
              <i class="fas fa-globe"></i> 只看公开文件
            </div>
            <div class="suggestion-item" @mousedown.prevent="applySuggestion('is:private')">
              <i class="fas fa-lock"></i> 只看私有文件
            </div>
            <div class="suggestion-divider"></div>
            <div class="suggestion-item" @mousedown.prevent="applySuggestion('tag:landscape')">
              <i class="fas fa-tag"></i> #landscape
            </div>
            <div class="suggestion-item" @mousedown.prevent="applySuggestion('tag:design')">
              <i class="fas fa-tag"></i> #design
            </div>
            <div class="suggestion-item" @mousedown.prevent="applySuggestion('tag:screenshot')">
              <i class="fas fa-tag"></i> #screenshot
            </div>
            <div class="suggestion-divider"></div>
            <div class="suggestion-item suggestion-ai" @mousedown.prevent="applySuggestion('ocr:')">
              <i class="fas fa-font"></i> 搜索图片中的文字 (OCR)
              <span class="suggestion-badge">即将推出</span>
            </div>
          </div>
        </div>
        <div style="display:flex;gap:4px">
          <button class="view-btn" :class="{active: filesStore.viewMode==='gallery'}" @click="filesStore.setViewMode('gallery')" title="网格"><i class="fas fa-th"></i></button>
          <button class="view-btn" :class="{active: filesStore.viewMode==='list'}" @click="filesStore.setViewMode('list')" title="列表"><i class="fas fa-list"></i></button>
          <button class="view-btn" :class="{active: filesStore.viewMode==='masonry'}" @click="filesStore.setViewMode('masonry')" title="瀑布流"><i class="fas fa-stream"></i></button>
          <button class="view-btn" :class="{active: filesStore.viewMode==='timeline'}" @click="filesStore.setViewMode('timeline')" title="时间轴"><i class="fas fa-clock"></i></button>
        </div>
      </div>
    </div>

    <div v-if="selectionStore.selectionMode" class="batch-bar">
      <span>已选择 {{ selectionStore.selectedCount }} 个文件</span>
      <button class="batch-btn" @click="batchDelete"><i class="fas fa-trash"></i> 批量删除</button>
      <button class="batch-btn" @click="selectionStore.selectNone()"><i class="fas fa-times"></i> 取消选择</button>
    </div>

    <div v-if="filesStore.files.length === 0 && !filesStore.loading" class="empty-state">
      <div class="empty-icon"><i class="fas fa-folder-open"></i></div>
      <h3>还没有文件</h3>
      <p>上传你的第一个文件开始使用</p>
      <router-link to="/upload" class="btn-primary" style="display:inline-block;text-decoration:none">去上传</router-link>
    </div>

    <div v-else-if="filesStore.viewMode === 'gallery'" class="file-grid">
      <div v-for="file in filesStore.files" :key="file.file_id" class="file-card" :class="{ selected: selectionStore.isSelected(file.file_id) }" @click="selectionStore.selectionMode ? selectionStore.toggleSelect(file.file_id) : detailFile = file">
        <div class="card-cover">
          <img v-if="isImage(file)" :src="getImageSrc(file)" loading="lazy" />
          <div v-else class="card-cover-icon" :style="{color: getFileColor(file)}"><i :class="getFileIcon(file)"></i></div>
        </div>
        <div class="card-content">
          <div class="card-title" :title="file.filename">{{ file.filename }}</div>
          <div class="card-meta">{{ formatSize(file.size) }} · {{ formatDate(file.upload_time) }}</div>
          <div class="card-actions">
            <button class="card-btn" @click.stop="copyLink(file)">链接</button>
            <button class="card-btn" @click.stop="copyMd(file)">MD</button>
            <button class="card-btn" :class="{ primary: file.is_public }" @click.stop="togglePublic(file)">{{ file.is_public ? '公开' : '私有' }}</button>
            <button class="card-btn danger" @click.stop="deleteFile(file)">删除</button>
          </div>
        </div>
      </div>
    </div>

    <div v-else-if="filesStore.viewMode === 'masonry'" class="masonry-view">
      <div v-for="file in filesStore.files" :key="file.file_id" class="masonry-item" @click="detailFile = file">
        <img v-if="isImage(file)" :src="getImageSrc(file)" loading="lazy" />
        <div v-else class="masonry-icon" :style="{background: getFileColor(file)+'18', color: getFileColor(file)}">
          <i :class="getFileIcon(file)"></i>
        </div>
        <div class="masonry-info">
          <div class="masonry-name">{{ file.filename }}</div>
          <div class="masonry-meta">{{ formatSize(file.size) }}</div>
        </div>
      </div>
    </div>

    <div v-else-if="filesStore.viewMode === 'timeline'" class="timeline-view">
      <div v-for="group in timelineGroups" :key="group.label" class="timeline-group">
        <div class="timeline-label">{{ group.label }} <span class="timeline-count">{{ group.count }} 个文件</span></div>
        <div class="timeline-grid">
          <div v-for="file in group.files" :key="file.file_id" class="timeline-item" @click="detailFile = file">
            <img v-if="isImage(file)" :src="getImageSrc(file)" loading="lazy" />
            <div v-else class="timeline-icon" :style="{background: getFileColor(file)+'18', color: getFileColor(file)}">
              <i :class="getFileIcon(file)"></i>
            </div>
            <div class="timeline-name">{{ file.filename }}</div>
          </div>
        </div>
      </div>
    </div>

    <div v-else class="list-view">
      <div class="list-header"><span></span><span>文件名</span><span>大小</span><span>时间</span><span>操作</span></div>
      <div v-for="file in filesStore.files" :key="file.file_id" class="list-row" :class="{ selected: selectionStore.isSelected(file.file_id) }" @click="selectionStore.selectionMode ? selectionStore.toggleSelect(file.file_id) : detailFile = file">
        <div class="list-icon" :style="{background: getFileColor(file)+'18', color: getFileColor(file)}"><i :class="getFileIcon(file)"></i></div>
        <div class="list-name">{{ file.filename }} <span v-if="file.is_public" class="badge">公开</span></div>
        <div class="list-size">{{ formatSize(file.size) }}</div>
        <div class="list-date">{{ formatDate(file.upload_time) }}</div>
        <div class="list-actions">
          <button @click.stop="copyLink(file)"><i class="fas fa-link"></i></button>
          <button @click.stop="copyMd(file)"><i class="fas fa-font"></i></button>
          <button @click.stop="togglePublic(file)" :style="file.is_public ? 'color:#10b981' : ''"><i :class="file.is_public ? 'fas fa-globe' : 'fas fa-lock'"></i></button>
          <button class="danger" @click.stop="deleteFile(file)"><i class="fas fa-trash"></i></button>
        </div>
      </div>
    </div>

    <div v-if="filesStore.total > 0" style="margin-top:24px;display:flex;justify-content:center">
      <el-pagination v-model:current-page="filesStore.page" v-model:page-size="filesStore.pageSize" :page-sizes="[10,20,50,100]" :total="filesStore.total" layout="total, sizes, prev, pager, next" @size-change="filesStore.loadFiles()" @current-change="filesStore.loadFiles()" />
    </div>

    <ImageViewer :visible="!!selectedFile" :file="selectedFile" :files="filesStore.files" @close="selectedFile = null" @prev="navigateFile(-1)" @next="navigateFile(1)" />

    <FileDetailDrawer :visible="!!detailFile" :file="detailFile" @close="detailFile = null" @refresh="filesStore.loadFiles()" />
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useFilesStore } from '../stores/files'
import { useSelectionStore } from '../stores/selection'
import { ElMessage } from 'element-plus'
import api from '../utils/api'
import ImageViewer from '../components/ImageViewer.vue'
import FileDetailDrawer from '../components/FileDetailDrawer.vue'

const filesStore = useFilesStore()
const selectionStore = useSelectionStore()
const selectedFile = ref(null)
const detailFile = ref(null)
const searchQuery = ref('')
const showSuggestions = ref(false)

function handleSearch() {
  const q = searchQuery.value.trim()
  if (q.startsWith('type:')) {
    filesStore.typeFilter = q.replace('type:', '')
    filesStore.search = ''
  } else if (q === 'is:public') {
    filesStore.search = ''
    filesStore.typeFilter = ''
  } else if (q === 'is:private') {
    filesStore.search = ''
    filesStore.typeFilter = ''
  } else {
    filesStore.search = q
    filesStore.typeFilter = ''
  }
  filesStore.page = 1
  filesStore.loadFiles()
  showSuggestions.value = false
}

function applySuggestion(suggestion) {
  searchQuery.value = suggestion
  handleSearch()
}

function clearSearch() {
  searchQuery.value = ''
  filesStore.search = ''
  filesStore.typeFilter = ''
  filesStore.page = 1
  filesStore.loadFiles()
}

function hideSuggestions() {
  setTimeout(() => { showSuggestions.value = false }, 150)
}

function handleKeydown(e) {
  if ((e.ctrlKey || e.metaKey) && e.key === 'a') {
    e.preventDefault()
    selectionStore.selectAll(filesStore.files)
  }
  if (e.key === 'Escape') {
    if (selectedFile.value) {
      selectedFile.value = null
    } else {
      selectionStore.selectNone()
    }
  }
  if (selectedFile.value) {
    if (e.key === 'ArrowLeft') navigateFile(-1)
    if (e.key === 'ArrowRight') navigateFile(1)
  }
}

function navigateFile(dir) {
  if (!selectedFile.value) return
  const idx = filesStore.files.findIndex(x => x.file_id === selectedFile.value.file_id)
  const next = idx + dir
  if (next >= 0 && next < filesStore.files.length) {
    selectedFile.value = filesStore.files[next]
  }
}

const timelineGroups = computed(() => {
  const now = new Date()
  const today = new Date(now.getFullYear(), now.getMonth(), now.getDate())
  const yesterday = new Date(today - 86400000)
  const weekStart = new Date(today - (today.getDay() || 7) * 86400000)
  const lastWeekStart = new Date(weekStart - 7 * 86400000)
  const monthStart = new Date(now.getFullYear(), now.getMonth(), 1)
  const lastMonthStart = new Date(now.getFullYear(), now.getMonth() - 1, 1)

  const groups = {}
  filesStore.files.forEach(f => {
    const d = new Date(f.upload_time * 1000)
    let label
    if (d >= today) label = '今天'
    else if (d >= yesterday) label = '昨天'
    else if (d >= weekStart) label = '本周'
    else if (d >= lastWeekStart) label = '上周'
    else if (d >= monthStart) label = '本月'
    else if (d >= lastMonthStart) label = '上月'
    else {
      const y = d.getFullYear()
      const m = d.getMonth() + 1
      label = `${y}年${m}月`
    }
    if (!groups[label]) groups[label] = []
    groups[label].push(f)
  })

  return Object.entries(groups).map(([label, files]) => ({
    label,
    count: files.length,
    files
  }))
})

onMounted(() => { filesStore.loadFiles(); window.addEventListener('keydown', handleKeydown) })
onUnmounted(() => { window.removeEventListener('keydown', handleKeydown) })

const tabs = computed(() => [
  { key: '', label: '全部', icon: 'fas fa-layer-group', count: filesStore.total },
  { key: 'image', label: '图片', icon: 'fas fa-image', count: '-' },
  { key: 'document', label: '文档', icon: 'fas fa-file-alt', count: '-' },
  { key: 'video', label: '视频', icon: 'fas fa-video', count: '-' },
  { key: 'audio', label: '音频', icon: 'fas fa-music', count: '-' },
])

function isImage(f) { return f.mime_type?.startsWith('image/') }
function getFileColor(f) {
  const ext = f.filename?.split('.').pop()?.toLowerCase()
  const map = { jpg:'#2563eb',png:'#2563eb',pdf:'#ef4444',doc:'#0ea5e9',xls:'#10b981',ppt:'#f97316',mp4:'#8b5cf6',mp3:'#ec4899',zip:'#78716c' }
  return map[ext] || '#64748b'
}
function getFileIcon(f) {
  const mime = f.mime_type || ''
  if (mime.includes('pdf')) return 'fas fa-file-pdf'
  if (mime.includes('word')) return 'fas fa-file-word'
  if (mime.includes('excel')) return 'fas fa-file-excell'
  if (mime.startsWith('video/')) return 'fas fa-file-video'
  if (mime.startsWith('audio/')) return 'fas fa-file-audio'
  return 'fas fa-file'
}
function getImageSrc(f) { return `/api/i/${f.file_id}?size=400&token=${localStorage.getItem('token') || ''}` }
function formatSize(b) { if(b<1024)return b+'B';if(b<1048576)return(b/1024).toFixed(1)+'KB';if(b<1073741824)return(b/1048576).toFixed(1)+'MB';return(b/1073741824).toFixed(2)+'GB' }
function formatDate(t) { return new Date(t*1000).toLocaleDateString('zh-CN') }
async function copyLink(f) { await navigator.clipboard.writeText(window.location.origin+'/api/i/'+f.file_id+'?token='+localStorage.getItem('token')); ElMessage.success('链接已复制') }
async function copyMd(f) { await navigator.clipboard.writeText(`![${f.filename}](${window.location.origin}/api/i/${f.file_id}?token=${localStorage.getItem('token')})`); ElMessage.success('Markdown已复制') }
async function togglePublic(f) { await api.put(`/file/${f.file_id}/public`); f.is_public = !f.is_public; ElMessage.success(f.is_public ? '已设为公开' : '已设为私有') }
async function deleteFile(f) { if(confirm('确定删除？')) { await api.delete(`/file/${f.file_id}`); filesStore.loadFiles(); ElMessage.success('已删除') } }
async function batchDelete() {
  const ids = selectionStore.getSelectedIds()
  if (!ids.length) return
  if (!confirm(`确定删除选中的 ${ids.length} 个文件？`)) return
  try {
    await Promise.all(ids.map(id => api.delete(`/file/${id}`)))
    selectionStore.selectNone()
    filesStore.loadFiles()
    ElMessage.success('批量删除成功')
  } catch (e) {
    ElMessage.error('部分文件删除失败')
    filesStore.loadFiles()
  }
}
</script>

<style scoped>
.toolbar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; flex-wrap: wrap; gap: 12px; }
.toolbar-left { display: flex; gap: 10px; flex-wrap: wrap; }
.toolbar-right { display: flex; align-items: center; gap: 10px; }
.search-wrapper { position: relative; }
.lib-search-box { width: 280px; height: 40px; border-radius: 12px; background: white; border: 1px solid #e5e7eb; display: flex; align-items: center; gap: 8px; padding: 0 12px; transition: .2s; }
.lib-search-box:focus-within { border-color: #2563eb; box-shadow: 0 0 0 3px rgba(37,99,235,.1); }
.lib-search-box input { width: 100%; border: none; outline: none; background: none; font-size: 13px; }
.search-clear { width: 20px; height: 20px; border-radius: 4px; border: none; background: #f3f4f6; color: #9ca3af; cursor: pointer; display: flex; align-items: center; justify-content: center; font-size: 10px; flex-shrink: 0; }
.search-clear:hover { background: #e5e7eb; color: #374151; }
.search-suggestions { position: absolute; top: 100%; left: 0; right: 0; background: white; border: 1px solid #e5e7eb; border-radius: 12px; box-shadow: 0 8px 24px rgba(0,0,0,.08); z-index: 100; margin-top: 4px; }
.suggestion-item { padding: 10px 14px; display: flex; align-items: center; gap: 10px; cursor: pointer; font-size: 13px; color: #374151; }
.suggestion-item:first-child { border-radius: 12px 12px 0 0; }
.suggestion-item:last-child { border-radius: 0 0 12px 12px; }
.suggestion-item:hover { background: #f3f4f6; }
.suggestion-item i { width: 16px; font-size: 12px; color: #6b7280; }
.suggestion-divider { height: 1px; background: #e5e7eb; margin: 4px 12px; }
.suggestion-badge { margin-left: auto; font-size: 10px; padding: 2px 6px; border-radius: 4px; background: #fef3c7; color: #f59e0b; font-weight: 600; }
.chip { height: 40px; padding: 0 16px; border-radius: 12px; background: white; display: flex; align-items: center; gap: 8px; font-size: 13px; font-weight: 700; cursor: pointer; border: 1px solid #e5e7eb; }
.chip:hover { transform: translateY(-2px); }
.chip.active { background: #2563eb; color: white; border-color: #2563eb; }
.chip .count { font-size: 11px; background: rgba(255,255,255,.2); padding: 1px 6px; border-radius: 8px; }
.chip.active .count { background: rgba(255,255,255,.3); }
.chip-badge { font-size: 9px; padding: 1px 5px; border-radius: 4px; background: #ede9fe; color: #7c3aed; font-weight: 700; }
.chip-ai { border-style: dashed; border-color: #c4b5fd; color: #7c3aed; }
.chip-ai:hover { background: #f5f3ff; border-color: #7c3aed; }
.file-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(340px, 1fr)); gap: 24px; margin-top: 24px; }
.file-card { background: white; border-radius: 30px; overflow: hidden; box-shadow: 0 16px 50px rgba(15,23,42,.05); transition: .22s; cursor: pointer; }
.file-card:hover { transform: translateY(-4px); }
.card-cover { height: 240px; overflow: hidden; background: #f3f4f6; }
.card-cover img { width: 100%; height: 100%; object-fit: cover; transition: .4s; }
.file-card:hover .card-cover img { transform: scale(1.04); }
.card-cover-icon { width: 100%; height: 100%; display: flex; align-items: center; justify-content: center; font-size: 3rem; }
.card-content { padding: 22px; }
.card-title { font-size: 16px; font-weight: 800; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.card-meta { margin-top: 8px; font-size: 13px; color: #6b7280; }
.card-actions { margin-top: 18px; display: flex; gap: 12px; }
.card-btn { flex: 1; height: 44px; border: none; border-radius: 14px; background: #f3f4f6; font-weight: 700; font-size: 13px; cursor: pointer; }
.card-btn:hover { transform: translateY(-2px); }
.card-btn.primary { background: #111827; color: white; }
.card-btn.danger { background: #fef2f2; color: #ef4444; }
.list-view { background: white; border-radius: 20px; overflow: hidden; box-shadow: 0 12px 40px rgba(15,23,42,.05); }
.list-header { display: grid; grid-template-columns: 44px 1fr 100px 140px 140px; padding: 12px 20px; background: #fafcff; border-bottom: 1px solid #eef2f7; font-size: 12px; font-weight: 700; color: #9ca3af; text-transform: uppercase; }
.list-row { display: grid; grid-template-columns: 44px 1fr 100px 140px 140px; padding: 14px 20px; align-items: center; border-bottom: 1px solid #f5f7fb; cursor: pointer; }
.list-row:hover { background: #f8fafc; }
.list-icon { width: 38px; height: 38px; border-radius: 10px; display: flex; align-items: center; justify-content: center; }
.list-name { font-size: 14px; font-weight: 600; display: flex; align-items: center; gap: 8px; }
.list-size, .list-date { font-size: 13px; color: #6b7280; }
.list-actions { display: flex; gap: 4px; justify-content: flex-end; }
.list-actions button { width: 32px; height: 32px; border-radius: 8px; border: none; background: transparent; color: #9ca3af; cursor: pointer; }
.list-actions button:hover { background: #f3f4f6; color: #2563eb; }
.list-actions button.danger:hover { background: #fef2f2; color: #ef4444; }
.badge { font-size: 11px; padding: 2px 8px; border-radius: 6px; background: #ecfdf5; color: #10b981; font-weight: 700; }
.empty-state { text-align: center; padding: 80px 20px; }
.empty-icon { width: 80px; height: 80px; border-radius: 50%; background: #eff6ff; display: inline-flex; align-items: center; justify-content: center; margin-bottom: 20px; }
.empty-icon i { font-size: 2rem; color: #2563eb; opacity: .6; }
.file-card.selected { border: 2px solid #2563eb; box-shadow: 0 0 0 2px rgba(37,99,235,.15); }
.list-row.selected { background: #eff6ff; border-left: 3px solid #2563eb; }
.batch-bar { display: flex; align-items: center; gap: 12px; padding: 12px 20px; margin-bottom: 16px; background: linear-gradient(135deg, #2563eb, #7c3aed); color: white; border-radius: 16px; font-size: 14px; font-weight: 600; }
.batch-btn { display: flex; align-items: center; gap: 6px; padding: 8px 16px; border: none; border-radius: 10px; background: rgba(255,255,255,.2); color: white; font-weight: 600; font-size: 13px; cursor: pointer; transition: .15s; }
.batch-btn:hover { background: rgba(255,255,255,.35); transform: translateY(-1px); }
.view-btn { width: 36px; height: 36px; border-radius: 10px; border: 1px solid #e5e7eb; background: white; cursor: pointer; display: flex; align-items: center; justify-content: center; font-size: 13px; color: #6b7280; }
.view-btn:hover { border-color: #2563eb; color: #2563eb; }
.view-btn.active { background: #2563eb; color: white; border-color: #2563eb; }
.masonry-view { columns: 3 300px; column-gap: 20px; margin-top: 24px; }
.masonry-item { break-inside: avoid; margin-bottom: 20px; border-radius: 20px; overflow: hidden; background: white; box-shadow: 0 12px 40px rgba(15,23,42,.05); cursor: pointer; transition: .2s; }
.masonry-item:hover { transform: translateY(-4px); }
.masonry-item img { width: 100%; display: block; }
.masonry-icon { padding: 40px; display: flex; align-items: center; justify-content: center; font-size: 2rem; }
.masonry-info { padding: 14px; }
.masonry-name { font-size: 14px; font-weight: 700; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.masonry-meta { font-size: 12px; color: #6b7280; margin-top: 4px; }
.timeline-group { margin-bottom: 32px; }
.timeline-label { font-size: 14px; font-weight: 700; color: #6b7280; margin-bottom: 14px; padding-bottom: 8px; border-bottom: 1px solid #e5e7eb; display: flex; align-items: center; gap: 8px; }
.timeline-count { font-size: 11px; font-weight: 600; color: #9ca3af; background: #f3f4f6; padding: 2px 8px; border-radius: 6px; }
.timeline-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(160px, 1fr)); gap: 14px; }
.timeline-item { border-radius: 14px; overflow: hidden; background: white; box-shadow: 0 8px 24px rgba(15,23,42,.04); cursor: pointer; transition: .2s; }
.timeline-item:hover { transform: translateY(-2px); }
.timeline-item img { width: 100%; aspect-ratio: 1; object-fit: cover; display: block; }
.timeline-icon { padding: 24px; display: flex; align-items: center; justify-content: center; font-size: 1.5rem; }
.timeline-name { padding: 10px 12px; font-size: 12px; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
</style>
