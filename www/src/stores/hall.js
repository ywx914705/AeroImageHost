import { defineStore } from 'pinia'
import { ref } from 'vue'
import api from '../utils/api'

export const useHallStore = defineStore('hall', () => {
  const posts = ref([])
  const total = ref(0)
  const loading = ref(false)
  const tags = ref([])
  const sort = ref('latest')
  const tagFilter = ref('')

  async function loadPosts() {
    loading.value = true
    try {
      const params = { offset: 0, limit: 50, sort: sort.value }
      if (tagFilter.value) params.tag = tagFilter.value
      const res = await api.get('/hall', { params })
      posts.value = res.data.posts || []
      total.value = res.data.total || 0
    } catch (e) {}
    loading.value = false
  }

  async function loadTags() {
    try {
      const res = await api.get('/hall/tags')
      tags.value = res.data.tags || []
    } catch (e) {}
  }

  async function toggleLike(post) {
    try {
      const res = await api.post(`/hall/${post.id}/like`)
      if (res.data) {
        post.likes = res.data.likes
        post.is_liked = res.data.is_liked
      }
    } catch (e) {}
  }

  return { posts, total, loading, tags, sort, tagFilter, loadPosts, loadTags, toggleLike }
})
