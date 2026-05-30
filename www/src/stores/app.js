import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import api from '../utils/api'

export const useAppStore = defineStore('app', () => {
  const token = ref(localStorage.getItem('token') || '')
  const username = ref(localStorage.getItem('username') || '')
  const userId = ref(parseInt(localStorage.getItem('user_id') || '0'))
  const theme = ref(localStorage.getItem('theme') || 'light')
  const stats = ref({})

  const isLoggedIn = computed(() => !!token.value)
  const isAdmin = computed(() => userId.value === stats.value.admin_user_id)

  async function login(account, password) {
    const res = await api.post('/auth/login', { account, password })
    token.value = res.data.token
    username.value = account
    localStorage.setItem('token', res.data.token)
    localStorage.setItem('username', account)
    return res.data
  }

  function logout() {
    token.value = ''
    username.value = ''
    userId.value = 0
    localStorage.clear()
  }

  async function loadStats() {
    try {
      const res = await api.get('/stats')
      stats.value = res.data
    } catch (e) {}
  }

  function toggleTheme() {
    theme.value = theme.value === 'light' ? 'dark' : 'light'
    document.documentElement.setAttribute('data-theme', theme.value)
    localStorage.setItem('theme', theme.value)
  }

  return { token, username, userId, theme, stats, isLoggedIn, isAdmin, login, logout, loadStats, toggleTheme }
})
