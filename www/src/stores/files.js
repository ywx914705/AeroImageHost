import { defineStore } from 'pinia'
import { ref } from 'vue'
import api from '../utils/api'

export const useFilesStore = defineStore('files', () => {
  const files = ref([])
  const total = ref(0)
  const loading = ref(false)
  const page = ref(1)
  const pageSize = ref(20)
  const search = ref('')
  const typeFilter = ref('')
  const sort = ref('time')
  const order = ref('desc')
  const viewMode = ref(localStorage.getItem('viewMode') || 'gallery')

  // 文件夹相关
  const folders = ref([])
  const currentFolderId = ref(null)
  const folderTree = ref([])

  async function loadFolders() {
    try {
      const res = await api.get('/folders')
      folderTree.value = res.data.data || []
    } catch (e) {}
  }

  async function createFolder(name, parentId = null) {
    try {
      const res = await api.post('/folders', { name, parent_id: parentId })
      await loadFolders()
      return res.data.data
    } catch (e) { throw e }
  }

  async function deleteFolder(id) {
    try {
      await api.delete(`/folders/${id}`)
      if (currentFolderId.value === id) currentFolderId.value = null
      await loadFolders()
    } catch (e) { throw e }
  }

  async function renameFolder(id, name) {
    try {
      await api.put(`/folders/${id}`, { name })
      await loadFolders()
    } catch (e) { throw e }
  }

  async function moveFolder(id, newParentId) {
    try {
      await api.put(`/folders/${id}/move`, { parent_id: newParentId })
      await loadFolders()
    } catch (e) { throw e }
  }

  async function loadFiles() {
    loading.value = true
    try {
      const params = {
        offset: (page.value - 1) * pageSize.value,
        limit: pageSize.value,
        search: search.value,
        type: typeFilter.value,
        sort: sort.value,
        order: order.value
      }
      const res = await api.get('/files', { params })
      files.value = res.data.files || []
      total.value = res.data.total || 0
    } catch (e) {
      console.error('Failed to load files:', e)
    }
    loading.value = false
  }

  function setViewMode(mode) {
    viewMode.value = mode
    localStorage.setItem('viewMode', mode)
  }

  return {
    files, total, loading, page, pageSize, search, typeFilter, sort, order, viewMode,
    folders, currentFolderId, folderTree,
    loadFiles, setViewMode, loadFolders, createFolder, deleteFolder, renameFolder, moveFolder
  }
})
