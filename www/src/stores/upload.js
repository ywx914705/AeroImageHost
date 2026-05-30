import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import api from '../utils/api'

export const useUploadStore = defineStore('upload', () => {
  const queue = ref([])
  const isUploading = ref(false)
  const uploadHistory = ref(JSON.parse(localStorage.getItem('uploadHistory') || '[]'))

  const completedCount = computed(() => queue.value.filter(i => i.status === 'completed').length)
  const failedCount = computed(() => queue.value.filter(i => i.status === 'failed').length)
  const uploadingCount = computed(() => queue.value.filter(i => i.status === 'uploading').length)
  const pendingCount = computed(() => queue.value.filter(i => i.status === 'pending').length)
  const pausedCount = computed(() => queue.value.filter(i => i.status === 'paused').length)
  const totalCount = computed(() => queue.value.length)

  const totalSpeed = computed(() => {
    return queue.value
      .filter(i => i.status === 'uploading')
      .reduce((sum, i) => sum + (i.speed || 0), 0)
  })

  function formatSize(bytes) {
    if (!bytes || bytes === 0) return '0 B'
    const units = ['B', 'KB', 'MB', 'GB']
    let i = 0
    let size = bytes
    while (size >= 1024 && i < units.length - 1) { size /= 1024; i++ }
    return size.toFixed(1) + ' ' + units[i]
  }

  function formatSpeed(bytesPerSec) {
    return formatSize(bytesPerSec) + '/s'
  }

  function formatEta(seconds) {
    if (!seconds || seconds === Infinity || isNaN(seconds)) return '--'
    if (seconds < 60) return Math.round(seconds) + 's'
    if (seconds < 3600) return Math.floor(seconds / 60) + 'm ' + Math.round(seconds % 60) + 's'
    return Math.floor(seconds / 3600) + 'h ' + Math.floor((seconds % 3600) / 60) + 'm'
  }

  function createThumbnail(file) {
    if (!file || !file.type.startsWith('image/')) return null
    try {
      return URL.createObjectURL(file)
    } catch {
      return null
    }
  }

  function addFiles(fileList) {
    for (const file of fileList) {
      const thumbnail = createThumbnail(file)
      queue.value.push({
        id: Math.random().toString(36).slice(2),
        file,
        name: file.name,
        size: file.size,
        formattedSize: formatSize(file.size),
        progress: 0,
        status: 'pending',
        speed: 0,
        formattedSpeed: '0 B/s',
        eta: '',
        error: null,
        retryCount: 0,
        thumbnail,
        _controller: null,
        _startTime: null,
        _lastLoaded: 0,
        _lastTime: null,
        url: null
      })
    }
  }

  async function uploadNext() {
    const item = queue.value.find(i => i.status === 'pending')
    if (!item) {
      isUploading.value = false
      return
    }
    item.status = 'uploading'
    item._startTime = Date.now()
    item._lastLoaded = 0
    item._lastTime = Date.now()
    isUploading.value = true

    const controller = new AbortController()
    item._controller = controller

    try {
      const formData = new FormData()
      formData.append('file', item.file)
      const res = await api.post('/upload', formData, {
        signal: controller.signal,
        onUploadProgress: (e) => {
          if (e.lengthComputable) {
            item.progress = Math.round((e.loaded / e.total) * 100)

            // Calculate speed
            const now = Date.now()
            const timeDiff = (now - (item._lastTime || now)) / 1000
            if (timeDiff > 0.3) {
              const bytesDiff = e.loaded - (item._lastLoaded || 0)
              item.speed = Math.round(bytesDiff / timeDiff)
              item.formattedSpeed = formatSpeed(item.speed)
              item._lastLoaded = e.loaded
              item._lastTime = now

              // Calculate ETA
              const remaining = e.total - e.loaded
              const etaSeconds = item.speed > 0 ? remaining / item.speed : Infinity
              item.eta = formatEta(etaSeconds)
            }
          }
        }
      })
      item.status = 'completed'
      item.progress = 100
      item.speed = 0
      item.formattedSpeed = ''
      item.eta = ''
      item.url = res.data?.url || res.data?.data?.url || null

      addHistory(item)
    } catch (e) {
      if (e.name === 'CanceledError' || e.name === 'AbortError') {
        // Paused or cancelled - status already set
        if (item.status === 'uploading') {
          item.status = 'paused'
        }
        item.speed = 0
        item.formattedSpeed = ''
        item.eta = ''
        uploadNext()
        return
      }
      item.retryCount++
      if (item.retryCount < 3) {
        item.status = 'pending'
        item.error = null
      } else {
        item.status = 'failed'
        item.error = e.message || '上传失败'
      }
      item.speed = 0
      item.formattedSpeed = ''
      item.eta = ''
    }
    uploadNext()
  }

  function startUpload() {
    uploadNext()
  }

  function pauseUpload(id) {
    const item = queue.value.find(i => i.id === id)
    if (item && item.status === 'uploading') {
      item.status = 'paused'
      item._controller?.abort()
      item.speed = 0
      item.formattedSpeed = ''
      item.eta = ''
    }
  }

  function resumeUpload(id) {
    const item = queue.value.find(i => i.id === id)
    if (item && item.status === 'paused') {
      item.status = 'pending'
      item.error = null
      uploadNext()
    }
  }

  function cancelUpload(id) {
    const item = queue.value.find(i => i.id === id)
    if (item) {
      item._controller?.abort()
      if (item.thumbnail) URL.revokeObjectURL(item.thumbnail)
      queue.value = queue.value.filter(i => i.id !== id)
    }
  }

  function retryUpload(id) {
    const item = queue.value.find(i => i.id === id)
    if (item && item.status === 'failed') {
      item.status = 'pending'
      item.error = null
      item.retryCount = 0
      item.progress = 0
      uploadNext()
    }
  }

  function pauseAll() {
    queue.value.forEach(item => {
      if (item.status === 'uploading') {
        item.status = 'paused'
        item._controller?.abort()
        item.speed = 0
        item.formattedSpeed = ''
        item.eta = ''
      }
    })
  }

  function resumeAll() {
    let needsStart = false
    queue.value.forEach(item => {
      if (item.status === 'paused') {
        item.status = 'pending'
        item.error = null
        needsStart = true
      }
    })
    if (needsStart) uploadNext()
  }

  function clearCompleted() {
    queue.value.forEach(item => {
      if (item.status === 'completed' && item.thumbnail) {
        URL.revokeObjectURL(item.thumbnail)
      }
    })
    queue.value = queue.value.filter(i => i.status !== 'completed')
  }

  function clearAll() {
    queue.value.forEach(item => {
      item._controller?.abort()
      if (item.thumbnail) URL.revokeObjectURL(item.thumbnail)
    })
    queue.value = []
    isUploading.value = false
  }

  function addHistory(item) {
    const entry = {
      name: item.name,
      size: item.size,
      formattedSize: item.formattedSize,
      time: Date.now(),
      status: 'success',
      url: item.url
    }
    uploadHistory.value.unshift(entry)
    if (uploadHistory.value.length > 100) {
      uploadHistory.value = uploadHistory.value.slice(0, 100)
    }
    localStorage.setItem('uploadHistory', JSON.stringify(uploadHistory.value))
  }

  function clearHistory() {
    uploadHistory.value = []
    localStorage.removeItem('uploadHistory')
  }

  function formatHistoryTime(timestamp) {
    const diff = Date.now() - timestamp
    if (diff < 60000) return '刚刚'
    if (diff < 3600000) return Math.floor(diff / 60000) + ' 分钟前'
    if (diff < 86400000) return Math.floor(diff / 3600000) + ' 小时前'
    return new Date(timestamp).toLocaleDateString('zh-CN')
  }

  return {
    queue,
    isUploading,
    uploadHistory,
    completedCount,
    failedCount,
    uploadingCount,
    pendingCount,
    pausedCount,
    totalCount,
    totalSpeed,
    addFiles,
    startUpload,
    pauseUpload,
    resumeUpload,
    cancelUpload,
    retryUpload,
    pauseAll,
    resumeAll,
    clearCompleted,
    clearAll,
    clearHistory,
    formatSize,
    formatSpeed,
    formatHistoryTime
  }
})
