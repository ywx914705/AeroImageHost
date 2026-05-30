<template>
  <div class="login-page">
    <div class="login-card">
      <div class="login-logo">
        <div class="logo-box"><i class="fas fa-cloud"></i></div>
        <h1>AeroImageHost</h1>
        <p>高性能图床平台</p>
      </div>
      <el-tabs v-model="activeTab">
        <el-tab-pane label="登录" name="login">
          <el-form :model="loginForm" :rules="loginRules" ref="loginFormRef">
            <el-form-item prop="account"><el-input v-model="loginForm.account" placeholder="账号" prefix-icon="User" /></el-form-item>
            <el-form-item prop="password"><el-input v-model="loginForm.password" type="password" placeholder="密码" prefix-icon="Lock" show-password /></el-form-item>
            <el-button type="primary" @click="handleLogin" :loading="loading" style="width:100%;height:48px;border-radius:40px;font-weight:600">登 录</el-button>
          </el-form>
        </el-tab-pane>
        <el-tab-pane label="注册" name="register">
          <el-form :model="registerForm" :rules="registerRules" ref="registerFormRef">
            <el-form-item prop="account"><el-input v-model="registerForm.account" placeholder="账号" prefix-icon="User" /></el-form-item>
            <el-form-item prop="password"><el-input v-model="registerForm.password" type="password" placeholder="密码" prefix-icon="Lock" show-password /></el-form-item>
            <el-button type="primary" @click="handleRegister" :loading="loading" style="width:100%;height:48px;border-radius:40px;font-weight:600">注 册</el-button>
          </el-form>
        </el-tab-pane>
      </el-tabs>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive } from 'vue'
import { useRouter } from 'vue-router'
import { useAppStore } from '../stores/app'
import { ElMessage } from 'element-plus'
import api from '../utils/api'

const router = useRouter()
const appStore = useAppStore()
const activeTab = ref('login')
const loading = ref(false)
const loginForm = reactive({ account: '', password: '' })
const registerForm = reactive({ account: '', password: '' })
const loginRules = { account: [{required:true,message:'请输入账号'}], password: [{required:true,message:'请输入密码'}] }
const registerRules = { account: [{required:true,message:'请输入账号'}], password: [{required:true,message:'请输入密码'}] }

async function handleLogin() {
  loading.value = true
  try {
    await appStore.login(loginForm.account, loginForm.password)
    ElMessage.success('登录成功')
    router.push('/')
  } catch (e) {
    ElMessage.error(e.response?.data?.error || '登录失败')
  }
  loading.value = false
}

async function handleRegister() {
  loading.value = true
  try {
    await api.post('/auth/register', registerForm)
    ElMessage.success('注册成功，请登录')
    activeTab.value = 'login'
  } catch (e) {
    ElMessage.error(e.response?.data?.error || '注册失败')
  }
  loading.value = false
}
</script>

<style scoped>
.login-page { min-height: 100vh; display: flex; align-items: center; justify-content: center; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); }
.login-card { width: 420px; background: white; border-radius: 24px; padding: 40px; box-shadow: 0 20px 60px rgba(0,0,0,.15); }
.login-logo { text-align: center; margin-bottom: 30px; }
.logo-box { width: 64px; height: 64px; border-radius: 18px; background: linear-gradient(135deg, #2563eb, #7c3aed); display: inline-flex; align-items: center; justify-content: center; color: white; font-size: 1.5rem; margin-bottom: 16px; }
.login-logo h1 { font-size: 24px; font-weight: 800; }
.login-logo p { color: #6b7280; margin-top: 4px; }
</style>
